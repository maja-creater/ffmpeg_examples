
#define GLFW_INCLUDE_ES3
#include <GLFW/glfw3.h>

#include<stdio.h>
#include<stdint.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<errno.h>
#include<assert.h>

#include<sys/uio.h>
#include<sys/types.h>
#include<fcntl.h>

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/opt.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"

const char* vertex_shader_text =
	"#version 300 es \n"
	"layout (location = 0) in vec4 a_position; \n"
	"layout (location = 1) in vec2 a_texCoord; \n"
	"out vec2 v_texCoord; \n"
	"void main() \n"
	"{ \n"
	"    gl_Position = a_position; \n"
	"    v_texCoord  = a_texCoord; \n"
	"} \n";

const char* fragment_shader_text =
	"#version 300 es \n"
	"precision mediump float; \n"
	"in vec2 v_texCoord; \n"
	"layout (location = 0) out vec4 outColor;\n"
	"uniform sampler2D s_y; \n"
	"uniform sampler2D s_u; \n"
	"uniform sampler2D s_v; \n"
	"void main() \n"
	"{ \n"
	"    vec2 xy = v_texCoord; \n"
	"    float y = texture2D(s_y, xy).r; \n"
	"    float u = texture2D(s_u, xy).r - 0.5; \n"
	"    float v = texture2D(s_v, xy).r - 0.5; \n"
	"    float r = y + 1.4075 * v; \n"
	"    float g = y - 0.3455 * u - 0.7169 * v; \n"
	"    float b = y + 1.779  * u; \n"
	"    outColor = vec4(r, g, b, 1.0); \n"
	"} \n";


const GLfloat vert_array[] = {
	-1.0f,  -1.0f,
	1.0f,  -1.0f,
	-1.0f,   1.0f,
	1.0f,   1.0f
};

const GLfloat texture_array[] = {
	0.0f,   1.0f,
	1.0f,   1.0f,
	0.0f,   0.0f,
	1.0f,   0.0f,
};

GLuint program;

GLuint uniform_y;
GLuint uniform_u;
GLuint uniform_v;

GLuint texture_y;
GLuint texture_u;
GLuint texture_v;

#define WIDTH  640
#define HEIGHT 360

AVFormatContext* fmt_ctx        = NULL;
AVCodecContext*  video_dec_ctx  = NULL;
int              video_index    = -1;

AVFilterContext* buffersrc_ctx  = NULL;
AVFilterContext* buffersink_ctx = NULL;
AVFilterGraph*   filter_graph   = NULL;

int init()
{
	GLint status = 0;

	GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertex_shader, 1, &vertex_shader_text, NULL);
	glCompileShader(vertex_shader);
	glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &status);
	printf("#0, status: %d\n", status);

	GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragment_shader, 1, &fragment_shader_text, NULL);
	glCompileShader(fragment_shader);
	glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &status);
	printf("#1, status: %d\n", status);

	program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	printf("#2, status: %d\n", status);

	glGenTextures(1, &texture_y);
	glBindTexture(GL_TEXTURE_2D, texture_y);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, WIDTH, HEIGHT, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenTextures(1, &texture_u);
	glBindTexture(GL_TEXTURE_2D, texture_u);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, WIDTH / 2, HEIGHT / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glGenTextures(1, &texture_v);
	glBindTexture(GL_TEXTURE_2D, texture_v);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, WIDTH / 2, HEIGHT / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return 0;
}

int ffmpeg_init(const char* filename)
{
	int ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL);
	if (ret < 0) {
		printf("avformat_open_input error, ret: %d, %s\n", ret, av_err2str(ret));
		return -1;
	}

	ret = avformat_find_stream_info(fmt_ctx, NULL);
	if (ret < 0) {
		printf("avformat_find_stream_info error, ret: %d, %s\n", ret, av_err2str(ret));
		return -1;
	}

	AVStream* s = NULL;
	int i;
	for (i = 0; i < fmt_ctx->nb_streams; i++) {
		s  = fmt_ctx->streams[i];

		if (AVMEDIA_TYPE_VIDEO == s->codecpar->codec_type) {
			video_index = i;
			break;
		}
	}

	if (i == fmt_ctx->nb_streams) {
		printf("video stream not found\n");
		return -1;
	}

	AVCodec* c = avcodec_find_decoder(s->codecpar->codec_id);
	if (!c) {
		printf("decoder not found, codec_id: %d\n", s->codecpar->codec_id);
		return -1;
	}

	video_dec_ctx = avcodec_alloc_context3(c);
	if (!video_dec_ctx) {
		printf("avcodec_alloc_context3 error\n");
		return -1;
	}

	ret = avcodec_parameters_to_context(video_dec_ctx, s->codecpar);
	if (ret < 0) {
		printf("avcodec_parameters_to_context error, ret: %d, %s\n", ret, av_err2str(ret));
		return -1;
	}

	ret = avcodec_open2(video_dec_ctx, c, NULL);
	if (ret < 0) {
		printf("avcodec_parameters_to_context error, ret: %d, %s\n", ret, av_err2str(ret));
		return -1;
	}

	return 0;
}

static int init_filters(const char *filters_descr, AVCodecContext* dec_ctx, AVStream* video_stream)
{
    char args[512];
    int ret = 0;

    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");

	AVFilterInOut  *outputs    = avfilter_inout_alloc();
    AVFilterInOut  *inputs     = avfilter_inout_alloc();

    AVRational      time_base  = video_stream->time_base;

    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
            time_base.num, time_base.den,
            dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
    if (ret < 0)
        goto end;

    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
    if (ret < 0)
        goto end;

    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0)
		goto end;

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr, &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return ret;
}

int main(int argc, char* argv[])
{
	if (ffmpeg_init(argv[1]) < 0) {
		printf("ffmpeg_init error\n");
		return -1;
	}

	char* filter_str = malloc(256);
	if (!filter_str)
		return -1;
	snprintf(filter_str, 255, "scale=%dx%d", WIDTH, HEIGHT);

	int ret = init_filters(filter_str, video_dec_ctx, fmt_ctx->streams[video_index]);
	free(filter_str);
	filter_str = NULL;
	if (ret < 0) {
		printf("ffmpeg_init error\n");
		return -1;
	}

	GLFWwindow* window;

	if (!glfwInit())
		return -1;

	window = glfwCreateWindow(WIDTH, HEIGHT, "yuv420p", NULL, NULL);
	if (!window) {
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(window);
	glViewport(0, 0, WIDTH, HEIGHT);

	init();

	glUseProgram(program);
	uniform_y = glGetUniformLocation(program, "s_y");
	uniform_u = glGetUniformLocation(program, "s_u");
	uniform_v = glGetUniformLocation(program, "s_v");

	printf("uniform_y: %d\n", uniform_y);
	printf("uniform_u: %d\n", uniform_u);
	printf("uniform_v: %d\n", uniform_v);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vert_array);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, texture_array);
	glEnableVertexAttribArray(1);

	AVFrame* frame  = av_frame_alloc();
	AVFrame* frame2 = av_frame_alloc();
	if (!frame || !frame2) {
		printf("av_frame_alloc error\n");
		return -1;
	}

	int key_frame = 0;

	while (!glfwWindowShouldClose(window)) {
		uint8_t y[WIDTH * HEIGHT];
		uint8_t u[WIDTH * HEIGHT / 4];
		uint8_t v[WIDTH * HEIGHT / 4];

		AVPacket* pkt = av_packet_alloc();
		if (!pkt) {
			printf("av_packet_alloc error\n");
			return -1;
		}

		while (1) {
			int ret = av_read_frame(fmt_ctx, pkt);
			if (ret < 0) {
				printf("av_read_frame error, ret: %d, %s\n", ret, av_err2str(ret));
				av_packet_free(&pkt);
				return -1;
			}

			if (pkt->stream_index != video_index) {
				av_packet_unref(pkt);
				continue;
			}

			if (pkt->flags & AV_PKT_FLAG_KEY)
				key_frame++;

			if (key_frame > 0)
				break;

			av_packet_unref(pkt);
		}
		assert(pkt);

		int ret = avcodec_send_packet(video_dec_ctx, pkt);
		av_packet_free(&pkt);
		if (ret < 0) {
			printf("avcodec_send_packet error, ret: %d, %s\n", ret, av_err2str(ret));
			return -1;
		}

		while (ret >= 0) {
			ret = avcodec_receive_frame(video_dec_ctx, frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			} else if (ret < 0) {
				printf("avcodec_receive_frame error, ret: %d, %s\n", ret, av_err2str(ret));
				return -1;
			}

			frame->pts = frame->best_effort_timestamp;

			if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
				printf("av_buffersrc_add_frame_flags error\n");
				return -1;
			}

			while (1) {
				int ret2 = av_buffersink_get_frame(buffersink_ctx, frame2);
				if (ret2 == AVERROR(EAGAIN) || ret2 == AVERROR_EOF)
					break;
				if (ret2 < 0) {
					printf("av_buffersink_get_frame error, ret2: %d, %s\n", ret2, av_err2str(ret2));
					return -1;
				}

				int i;
				for (i = 0; i < frame2->height; i++)
					memcpy(y + i * frame2->width, frame2->data[0] + i * frame2->linesize[0], frame2->width);

				for (i = 0; i < frame2->height / 2; i++)
					memcpy(u + i * frame2->width / 2, frame2->data[1] + i * frame2->linesize[1], frame2->width / 2);

				for (i = 0; i < frame2->height / 2; i++)
					memcpy(v + i * frame2->width / 2, frame2->data[2] + i * frame2->linesize[2], frame2->width / 2);

				av_frame_unref(frame2);

				glClear(GL_COLOR_BUFFER_BIT);

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, texture_y);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_LUMINANCE, GL_UNSIGNED_BYTE, y);
				glUniform1i(uniform_y, 0);

				glActiveTexture(GL_TEXTURE1);
				glBindTexture(GL_TEXTURE_2D, texture_u);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH / 2, HEIGHT / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, u);
				glUniform1i(uniform_u, 1);

				glActiveTexture(GL_TEXTURE2);
				glBindTexture(GL_TEXTURE_2D, texture_v);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH / 2, HEIGHT / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, v);
				glUniform1i(uniform_v, 2);

				glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

				glfwSwapBuffers(window);
				glfwPollEvents();
			}
			av_frame_unref(frame);
		}
	}
	glfwTerminate();
	return 0;
}

