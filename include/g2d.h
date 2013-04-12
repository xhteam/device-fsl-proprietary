/*
 *  Copyright (C) 2013 Freescale Semiconductor, Inc.
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/*
 *	g2d.h
 *	Gpu 2d header file declare all g2d APIs exposed to application
 *	History :
 *	Date(y.m.d)        Author            Version        Description
 *	2012-10-22         Li Xianzhong      0.1            Created
 *	2013-02-22         Li Xianzhong      0.2            g2d_copy API is added
 *	2013-03-21         Li Xianzhong      0.4            g2d clear/rotation/flip APIs are supported
 *	2013-04-09         Li Xianzhong      0.5            g2d alpha blending feature is enhanced
*/

#ifndef __G2D_H__
#define __G2D_H__

#ifdef __cplusplus
extern "C"  {
#endif

enum g2d_format
{
//rgb formats
     G2D_RGB565               = 0,
     G2D_RGBA8888             = 1,
     G2D_RGBX8888             = 2,
     G2D_BGRA8888             = 3,
     G2D_BGRX8888             = 4,

//yuv formats
     G2D_NV12                 = 20,
     G2D_I420                 = 21,
};

enum g2d_blend_func
{
    G2D_ZERO                  = 0,
    G2D_ONE                   = 1,
    G2D_SRC_ALPHA             = 2,
    G2D_ONE_MINUS_SRC_ALPHA   = 3,
    G2D_DST_ALPHA             = 4,
    G2D_ONE_MINUS_DST_ALPHA   = 5,
};

enum g2d_cap_mode
{
    G2D_BLEND                 = 0,
    G2D_DITHER                = 1,
    G2D_GLOBAL_ALPHA          = 2,//only support source global alpha
};

enum g2d_rotation
{
    G2D_ROTATION_0            = 0,
    G2D_ROTATION_90           = 1,
    G2D_ROTATION_180          = 2,
    G2D_ROTATION_270          = 3,
    G2D_FLIP_H                = 4,
    G2D_FLIP_V                = 5,
};

struct g2d_surface
{
    enum g2d_format format;

    int planes[3];//physical address for plane
                  //RGB: only plane[0]is used
                  //I420SP: plane[0] - Y, plane[1] - UV
                  //I420: plane[0] - Y, plane[1] - U, plane[2] - V
    int left;
    int top;
    int right;
    int bottom;

    int stride;

    //alpha blending parameters
    enum g2d_blend_func blendfunc;

    //the global alpha value is 0 ~ 255
    int global_alpha;

    //clrcolor format is RGBA8888
    int clrcolor;

    //rotation degree
    enum g2d_rotation rot;
};

struct g2d_buf
{
    void *buf_handle;
    void *buf_vaddr;
    int  buf_paddr;
};

int g2d_open(void **handle);
int g2d_close(void *handle);

int g2d_clear(void *handle, struct g2d_surface *area);

int g2d_blit(void *handle, struct g2d_surface *src, struct g2d_surface *dst);
int g2d_copy(void *handle, struct g2d_buf *d, struct g2d_buf* s, int size);

int g2d_query_cap(void *handle, enum g2d_cap_mode cap, int *enable);
int g2d_enable(void *handle, enum g2d_cap_mode cap);
int g2d_disable(void *handle, enum g2d_cap_mode cap);

struct g2d_buf *g2d_alloc(int size);
int g2d_free(struct g2d_buf *buf);

#ifdef __cplusplus
}
#endif

#endif
