/*
 * Standalone command generator using virglrenderer's test encoder, based on its
 * test_virgl_cmd.c render setup. Upstream test code license follows.
 * Copyright (C) 2015 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testvirgl_encode.h"
#include "virgl_protocol.h"

static void overflow(struct virgl_context *ctx) { (void)ctx; abort(); }
int main(int argc,char **argv)
{
    if(argc!=4) return 2;
    unsigned frame=strtoul(argv[1],NULL,10),width=strtoul(argv[2],NULL,10),height=strtoul(argv[3],NULL,10);
    if(!width || !height) return 2;
    uint32_t commands[VIRGL_MAX_CMDBUF_DWORDS]={0};
    struct virgl_cmd_buf buffer={.buf=commands};
    struct virgl_context ctx={.cbuf=&buffer,.ctx_id=1,.flush=overflow};
    struct virgl_resource target={.handle=1},vbo={.handle=2};
    if(!frame) {
        struct virgl_surface surface={.handle=1,.base={.format=PIPE_FORMAT_R8G8B8A8_UNORM,.texture=&target.base}};
        virgl_encoder_create_surface(&ctx,1,&target,&surface.base);
        struct pipe_framebuffer_state fb={.nr_cbufs=1,.cbufs={&surface.base}};
        virgl_encoder_set_framebuffer_state(&ctx,&fb);
        struct pipe_vertex_element elements[2]={
            {.src_offset=0,.src_format=PIPE_FORMAT_R32G32B32A32_FLOAT},
            {.src_offset=16,.src_format=PIPE_FORMAT_R32G32B32A32_FLOAT}};
        virgl_encoder_create_vertex_elements(&ctx,2,2,elements);
        virgl_encode_bind_object(&ctx,2,VIRGL_OBJECT_VERTEX_ELEMENTS);
        struct pipe_shader_state state={0};
        const char *vs="VERT\nDCL IN[0]\nDCL IN[1]\nDCL OUT[0], POSITION\nDCL OUT[1], COLOR\n"
            "0: MOV OUT[1], IN[1]\n1: MOV OUT[0], IN[0]\n2: END\n";
        const char *fs="FRAG\nDCL IN[0], COLOR, LINEAR\nDCL OUT[0], COLOR\n0: MOV OUT[0], IN[0]\n1: END\n";
        virgl_encode_shader_state(&ctx,3,PIPE_SHADER_VERTEX,&state,vs);
        virgl_encode_shader_state(&ctx,4,PIPE_SHADER_FRAGMENT,&state,fs);
        virgl_encode_bind_shader(&ctx,3,PIPE_SHADER_VERTEX);
        virgl_encode_bind_shader(&ctx,4,PIPE_SHADER_FRAGMENT);
        uint32_t shaders[PIPE_SHADER_TYPES]={0};shaders[PIPE_SHADER_VERTEX]=3;shaders[PIPE_SHADER_FRAGMENT]=4;
        virgl_encode_link_shader(&ctx,shaders);
        for(unsigned blended=0;blended<2;blended++) {
            struct pipe_blend_state blend={0};
            blend.rt[0].colormask=PIPE_MASK_RGBA;
            blend.rt[0].blend_enable=blended;
            blend.rt[0].rgb_src_factor=PIPE_BLENDFACTOR_SRC_ALPHA;
            blend.rt[0].rgb_dst_factor=PIPE_BLENDFACTOR_INV_SRC_ALPHA;
            blend.rt[0].alpha_src_factor=PIPE_BLENDFACTOR_ONE;
            blend.rt[0].alpha_dst_factor=PIPE_BLENDFACTOR_INV_SRC_ALPHA;
            virgl_encode_blend_state(&ctx,blended?8:5,&blend);
        }
        struct pipe_depth_stencil_alpha_state dsa={0};
        virgl_encode_dsa_state(&ctx,6,&dsa);virgl_encode_bind_object(&ctx,6,VIRGL_OBJECT_DSA);
        struct pipe_rasterizer_state raster={.depth_clip=1,.half_pixel_center=1,.bottom_edge_rule=1,.point_size=1,.line_width=1};
        virgl_encode_rasterizer_state(&ctx,7,&raster);virgl_encode_bind_object(&ctx,7,VIRGL_OBJECT_RASTERIZER);
        struct pipe_viewport_state viewport={.scale={width/2.f,height/2.f,.5f},.translate={width/2.f,height/2.f,.5f}};
        virgl_encoder_set_viewport_states(&ctx,0,1,&viewport);
    }
    union pipe_color_union background={.f={.2f,.4f,.6f,1}};
    virgl_encode_clear(&ctx,PIPE_CLEAR_COLOR0,&background,0,0);
    virgl_encode_bind_object(&ctx,frame&1?8:5,VIRGL_OBJECT_BLEND);
    float color[4]={(frame*13%256)/255.f,(frame*29%256)/255.f,(frame*37%256)/255.f,(frame&1)?128/255.f:1};
    float vertices[3][8]={{-1,-1,0,1},{3,-1,0,1},{-1,3,0,1}};
    for(unsigned i=0;i<3;i++) memcpy(vertices[i]+4,color,sizeof(color));
    struct pipe_box box={.width=sizeof(vertices),.height=1,.depth=1};
    virgl_encoder_inline_write(&ctx,&vbo,0,0,&box,vertices,sizeof(vertices),0);
    struct pipe_vertex_buffer vb={.stride=sizeof(vertices[0]),.buffer=&vbo.base};
    virgl_encoder_set_vertex_buffers(&ctx,1,&vb);
    struct pipe_draw_info draw={.mode=PIPE_PRIM_TRIANGLES,.count=3,.instance_count=1,.max_index=2};
    virgl_encoder_draw_vbo(&ctx,&draw);
    return fwrite(commands,sizeof(*commands),buffer.cdw,stdout)==buffer.cdw?0:1;
}
