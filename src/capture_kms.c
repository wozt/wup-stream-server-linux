#define _GNU_SOURCE
#include "capture_kms.h"
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#define FW 854
#define FH 480
struct wup_kms {
    int fd, sw, sh, x, y, w, h, gpu;
    uint32_t crtc;
    int fb_x, fb_y;
    int screen_x, screen_y;
    struct gbm_device *gbm;
    struct gbm_bo *bo;
    void *map;
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    GLuint program, texture;
    PFNEGLCREATEIMAGEKHRPROC create_image;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind_image;
};
static int fail(const char *why) { fprintf(stderr,"[capture] KMS: %s (no fallback)\n",why); return -1; }
static void close_handles(int fd, drmModeFB2 *fb) {
    for (int i=0;i<4;i++) if(fb->handles[i]) {
        int duplicate=0;
        for(int j=0;j<i;j++) if(fb->handles[j]==fb->handles[i]) duplicate=1;
        if(!duplicate) { struct drm_gem_close c={0}; c.handle=fb->handles[i]; drmIoctl(fd,DRM_IOCTL_GEM_CLOSE,&c); }
    }
}
/* WUP_KMS_MULTI_MONITOR: match exact DRM names or AMD XRandR zero-based names. */
static int connector_matches(const char *xname, const char *drmname) {
    if (!strcmp(xname, drmname)) return 1;
    unsigned index; char tail; char candidate[64];
    if (sscanf(xname,"DisplayPort-%u%c",&index,&tail)==1 && index<1000) {
        snprintf(candidate,sizeof candidate,"DP-%u",index+1);
        return !strcmp(candidate,drmname);
    }
    if (sscanf(xname,"HDMI-A-%u%c",&index,&tail)==1 && index<1000) {
        snprintf(candidate,sizeof candidate,"HDMI-A-%u",index+1);
        return !strcmp(candidate,drmname);
    }
    return 0;
}
/* Legacy CRTC scanout coordinates are framebuffer-relative, not necessarily zero. */
static drmModeFB2 *framebuffer(wup_kms_t *k) {
    drmModeCrtc *c=drmModeGetCrtc(k->fd,k->crtc);
    if(!c) return NULL;
    uint32_t id=c->buffer_id;
    uint32_t fx=c->x, fy=c->y;
    int mode_ok=c->mode_valid && c->width==(uint32_t)k->sw && c->height==(uint32_t)k->sh;
    drmModeFreeCrtc(c);
    if(!id || !mode_ok) { fail("CRTC inactive or display mode changed"); return NULL; }
    drmModeFB2 *fb=drmModeGetFB2(k->fd,id);
    if(!fb) { fprintf(stderr,"[capture] KMS GetFB2 id=%u: %s\n",id,strerror(errno)); return NULL; }
    if(!fb->handles[0]) {
        fail("GetFB2 returned no GEM handle: DRM master or CAP_SYS_ADMIN required");
        drmModeFreeFB2(fb); return NULL;
    }
    if((fb->pixel_format!=DRM_FORMAT_XRGB8888 && fb->pixel_format!=DRM_FORMAT_ARGB8888) || fb->handles[1] ||
       (uint64_t)fx+k->x+k->w>fb->width || (uint64_t)fy+k->y+k->h>fb->height) {
        fprintf(stderr,"[capture] KMS unsupported FB %u: %ux%u format=%08x modifier=%llx offset=%u,%u extra_handle=%u\n",id,fb->width,fb->height,fb->pixel_format,(unsigned long long)fb->modifier,fx,fy,fb->handles[1]);
        close_handles(k->fd,fb); drmModeFreeFB2(fb); return NULL;
    }
    k->fb_x=(int)fx+k->x; k->fb_y=(int)fy+k->y;
    return fb;
}
static int export_frame(wup_kms_t *k,drmModeFB2 **fb,int *prime) {
    *fb=framebuffer(k); *prime=-1;
    if(!*fb) return fail("cannot access primary framebuffer; check permissions, format and geometry");
    int ret=drmPrimeHandleToFD(k->fd,(*fb)->handles[0],DRM_CLOEXEC|DRM_RDWR,prime);
    close_handles(k->fd,*fb);
    if(ret) { drmModeFreeFB2(*fb); *fb=NULL; return fail("DMA-BUF export failed"); }
    return 0;
}
static GLuint shader(GLenum kind,const char *source) {
    GLuint s=glCreateShader(kind); glShaderSource(s,1,&source,NULL); glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){ char log[512]={0}; glGetShaderInfoLog(s,sizeof log,NULL,log); fprintf(stderr,"KMS shader: %s\n",log); glDeleteShader(s); return 0; }
    return s;
}
static int egl_setup(wup_kms_t *k) {
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if(!get_display) return -1;
    k->display=get_display(EGL_PLATFORM_GBM_KHR,k->gbm,NULL);
    if(k->display==EGL_NO_DISPLAY || !eglInitialize(k->display,NULL,NULL) || !eglBindAPI(EGL_OPENGL_ES_API)) return -1;
    const char *ext=eglQueryString(k->display,EGL_EXTENSIONS);
    if(!ext || !strstr(ext,"EGL_EXT_image_dma_buf_import")) return -1;
    EGLint cfgattr[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig cfg; EGLint n=0;
    if(!eglChooseConfig(k->display,cfgattr,&cfg,1,&n)||!n) return -1;
    EGLint ctxattr[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE}, surfattr[]={EGL_WIDTH,FW,EGL_HEIGHT,FH,EGL_NONE};
    k->context=eglCreateContext(k->display,cfg,EGL_NO_CONTEXT,ctxattr);
    k->surface=eglCreatePbufferSurface(k->display,cfg,surfattr);
    if(k->context==EGL_NO_CONTEXT || k->surface==EGL_NO_SURFACE || !eglMakeCurrent(k->display,k->surface,k->surface,k->context)) return -1;
    k->create_image=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    k->destroy_image=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    k->bind_image=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    GLuint v=shader(GL_VERTEX_SHADER,"attribute vec2 pos; attribute vec2 uv; varying vec2 tex; void main(){gl_Position=vec4(pos,0.,1.);tex=uv;}");
    GLuint f=shader(GL_FRAGMENT_SHADER,"precision mediump float; varying vec2 tex; uniform sampler2D image; void main(){gl_FragColor=texture2D(image,tex).bgra;}");
    int ok=0;
    if(v&&f&&k->create_image&&k->destroy_image&&k->bind_image) {
        k->program=glCreateProgram(); glAttachShader(k->program,v); glAttachShader(k->program,f);
        glBindAttribLocation(k->program,0,"pos"); glBindAttribLocation(k->program,1,"uv"); glLinkProgram(k->program);
        glGetProgramiv(k->program,GL_LINK_STATUS,&ok); glGenTextures(1,&k->texture);
    }
    if (v) glDeleteShader(v);
    if (f) glDeleteShader(f);
    eglMakeCurrent(k->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    return ok?0:-1;
}
wup_kms_t *wup_kms_open(const char *output,int sw,int sh,int sx,int sy,int x,int y,int w,int h,int gpu) {
    if(sw<=0||sh<=0||x<0||y<0||w<=0||h<=0||x>sw-w||y>sh-h) return NULL;
    wup_kms_t *k=calloc(1,sizeof *k); if(!k) return NULL;
    k->fd=-1;k->sw=sw;k->sh=sh;k->x=x;k->y=y;k->w=w;k->h=h;k->gpu=gpu;
    k->screen_x=sx; k->screen_y=sy;
    glob_t paths={0};
    const char *forced=getenv("WUP_KMS_DEVICE");
    if(glob(forced&&*forced?forced:"/dev/dri/card[0-9]*",0,NULL,&paths)!=0) goto bad;
    for(size_t i=0;i<paths.gl_pathc;i++) {
        int fd=open(paths.gl_pathv[i],O_RDWR|O_CLOEXEC); if(fd<0) { fprintf(stderr,"[capture] KMS open %s: %s\n",paths.gl_pathv[i],strerror(errno)); continue; }
        drmModeRes *r=drmModeGetResources(fd);
        if(r) {
            for(int j=0;j<r->count_connectors;j++) {
                drmModeConnector *c=drmModeGetConnector(fd,r->connectors[j]); if(!c) continue;
                char name[64]; const char *type=drmModeGetConnectorTypeName(c->connector_type);
                snprintf(name,sizeof name,"%s-%u",type?type:"unknown",c->connector_type_id);
                if(c->connection==DRM_MODE_CONNECTED && c->encoder_id && connector_matches(output,name)) {
                    drmModeEncoder *e=drmModeGetEncoder(fd,c->encoder_id);
                    if(e&&e->crtc_id) {
                        drmModeCrtc *cr=drmModeGetCrtc(fd,e->crtc_id);
                        if(cr&&cr->mode_valid&&cr->width==(uint32_t)sw&&cr->height==(uint32_t)sh) {
                            fprintf(stderr,"[capture] KMS candidate %s -> %s CRTC %u origin %u,%u (X11 %d,%d)\n",output,name,e->crtc_id,cr->x,cr->y,sx,sy);
                            /* An individual scanout may have origin zero. A shared scanout must match XRandR. */
                            if ((cr->x==0 && cr->y==0) || (sx>=0 && sy>=0 && cr->x==(uint32_t)sx && cr->y==(uint32_t)sy)) { k->fd=fd;k->crtc=e->crtc_id; }
                        }
                        if(cr) drmModeFreeCrtc(cr);
                    }
                    if(e) drmModeFreeEncoder(e);
                }
                drmModeFreeConnector(c); if(k->fd>=0) break;
            }
            drmModeFreeResources(r);
        }
        if(k->fd>=0) { fprintf(stderr,"[capture] KMS %s: %s connector %s CRTC %u\n",gpu?"EGL":"CPU",paths.gl_pathv[i],output,k->crtc); break; }
        close(fd);
    }
    globfree(&paths);
    if(k->fd<0) goto bad;
    k->gbm=gbm_create_device(k->fd); if(!k->gbm) goto bad;
    drmModeFB2 *fb=NULL; int prime;
    if(export_frame(k,&fb,&prime)) goto bad;
    fprintf(stderr,"[capture] KMS framebuffer %u: %ux%u crop=%d,%d %dx%d modifier=%llx\n",fb->fb_id,fb->width,fb->height,k->fb_x,k->fb_y,k->w,k->h,(unsigned long long)fb->modifier);
    close(prime); drmModeFreeFB2(fb);
    if(gpu&&egl_setup(k)) { fail("EGL initialization failed"); goto bad; }
    fprintf(stderr,"[capture] DRM/KMS %s selected (no fallback)\n",gpu?"EGL":"CPU");
    return k;
bad:
    fail("initialization failed; verify connector name, permissions and primary scanout"); wup_kms_close(k); return NULL;
}
void wup_kms_release(wup_kms_t *k) {
    if(!k||!k->bo) return;
    if(k->map) gbm_bo_unmap(k->bo,k->map);
    gbm_bo_destroy(k->bo);k->bo=NULL;k->map=NULL;
}
int wup_kms_cpu(wup_kms_t *k,const uint8_t **pixels,int *stride) {
    if(!k||k->gpu) return -1;
    wup_kms_release(k);
    drmModeFB2 *fb=NULL;int prime;
    if(export_frame(k,&fb,&prime)) return -1;
    if(fb->flags & DRM_MODE_FB_MODIFIERS) {
        struct gbm_import_fd_modifier_data d={0};d.width=fb->width;d.height=fb->height;d.format=fb->pixel_format;d.num_fds=1;d.fds[0]=prime;d.strides[0]=fb->pitches[0];d.offsets[0]=fb->offsets[0];d.modifier=fb->modifier;
        k->bo=gbm_bo_import(k->gbm,GBM_BO_IMPORT_FD_MODIFIER,&d,0);
    } else if(!fb->offsets[0]) {
        struct gbm_import_fd_data d={0};d.fd=prime;d.width=fb->width;d.height=fb->height;d.stride=fb->pitches[0];d.format=fb->pixel_format;
        k->bo=gbm_bo_import(k->gbm,GBM_BO_IMPORT_FD,&d,0);
    }
    close(prime);drmModeFreeFB2(fb);
    if(!k->bo) return fail("GBM import failed");
    uint32_t pitch=0;
    void *ptr=gbm_bo_map(k->bo,k->fb_x,k->fb_y,k->w,k->h,GBM_BO_TRANSFER_READ,&pitch,&k->map);
    if(!ptr||pitch<(uint32_t)k->w*4||pitch>INT_MAX) {wup_kms_release(k);return fail("GBM CPU mapping failed");}
    *pixels=ptr;*stride=(int)pitch;return 0;
}
int wup_kms_egl(wup_kms_t *k,unsigned char *out,int x,int y,int w,int h) {
    if(!k||!k->gpu) return -1;
    drmModeFB2 *fb=NULL;int prime;
    if(export_frame(k,&fb,&prime)) return -1;
    if(!eglMakeCurrent(k->display,k->surface,k->surface,k->context)) {close(prime);drmModeFreeFB2(fb);return fail("EGL make current failed");}
    EGLint attrs[32]={EGL_WIDTH,(EGLint)fb->width,EGL_HEIGHT,(EGLint)fb->height,EGL_LINUX_DRM_FOURCC_EXT,(EGLint)fb->pixel_format,EGL_DMA_BUF_PLANE0_FD_EXT,prime,EGL_DMA_BUF_PLANE0_OFFSET_EXT,(EGLint)fb->offsets[0],EGL_DMA_BUF_PLANE0_PITCH_EXT,(EGLint)fb->pitches[0]};
    int n=12, ret=-1;
    if(fb->flags&DRM_MODE_FB_MODIFIERS) {
        const char *ext=eglQueryString(k->display,EGL_EXTENSIONS);
        if(!ext||!strstr(ext,"EGL_EXT_image_dma_buf_import_modifiers")) goto done;
        attrs[n++]=EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;attrs[n++]=(EGLint)(uint32_t)fb->modifier;
        attrs[n++]=EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;attrs[n++]=(EGLint)(uint32_t)(fb->modifier>>32);
    }
    attrs[n]=EGL_NONE;
    EGLImageKHR image=k->create_image(k->display,EGL_NO_CONTEXT,EGL_LINUX_DMA_BUF_EXT,NULL,attrs);
    if(image==EGL_NO_IMAGE_KHR) goto done;
    glBindTexture(GL_TEXTURE_2D,k->texture);k->bind_image(GL_TEXTURE_2D,(GLeglImageOES)image);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glViewport(0,0,FW,FH);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glViewport(x,y,w,h);glUseProgram(k->program);
    float l=(float)k->fb_x/fb->width,r=(float)(k->fb_x+k->w)/fb->width,t=(float)k->fb_y/fb->height,b=(float)(k->fb_y+k->h)/fb->height;
    const GLfloat pos[]={-1,-1,1,-1,-1,1,1,1},uv[]={l,t,r,t,l,b,r,b};
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,pos);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,uv);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glReadPixels(0,0,FW,FH,GL_RGBA,GL_UNSIGNED_BYTE,out);
    ret=glGetError()==GL_NO_ERROR?0:-1;
    /* Drop the imported scanout reference before next frame. */
    glDeleteTextures(1,&k->texture);glGenTextures(1,&k->texture);
    k->destroy_image(k->display,image);
done:
    close(prime);drmModeFreeFB2(fb);
    eglMakeCurrent(k->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    return ret?fail("EGL import/draw/readback failed"):0;
}
void wup_kms_close(wup_kms_t *k) {
    if(!k)return;
    wup_kms_release(k);
    if(k->display!=EGL_NO_DISPLAY) {
        if(k->context!=EGL_NO_CONTEXT&&k->surface!=EGL_NO_SURFACE&&eglMakeCurrent(k->display,k->surface,k->surface,k->context)) {
            if(k->texture)glDeleteTextures(1,&k->texture);
            if(k->program)glDeleteProgram(k->program);
            eglMakeCurrent(k->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        }
        if(k->surface!=EGL_NO_SURFACE)eglDestroySurface(k->display,k->surface);
        if(k->context!=EGL_NO_CONTEXT)eglDestroyContext(k->display,k->context);
        eglTerminate(k->display);
    }
    if(k->gbm)gbm_device_destroy(k->gbm);
    if(k->fd>=0)close(k->fd);
    free(k);
}
