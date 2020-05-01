/**
 * author :  luhz
 * email  :  luhzh5@mail2.sysu.edu.cn
 * breif  :  Live streaming using ffmpeg
 * data   :  2019-05-11
 * 
 * -------------------------------------------------------------
 *  author | version  |    data    |            breif
 * -------------------------------------------------------------
 *   luhz  |  v1.0.0  | 2019-05-11 |  读取摄像头并显示【本地】
 *   luhz  |  v1.0.1  | 2019-05-19 |  编码本地视频推流到服务器
 *   luhz  |  v1.0.2  | 2019-06-01 |  获取摄像头推流到服务器
 *   luhz  |  v1.0.3  | 2019-06-05 |  注释+优化，降低延迟，高并发
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <string>
extern "C"
{
#include "libavutil/opt.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/time.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/mathematics.h"
};

using namespace std;

// 设备名  device
//#define device_name "/dev/video0"

// 分辨率
//#define video_height     480
//#define video_width      640

class rtmp_data{
public:
    void init_input(const char* device, const char* resolution);
    void init_output(const char* out , const char* speed);
    void write_time();
	void decode_and_encode(int ret);
	int flush_encoder();
	void end_process();

private:
    AVFormatContext    *ifmt_ctx = NULL;
	AVFormatContext    *ofmt_ctx;

	AVInputFormat      *ifmt;
	AVStream           *video_st;
	AVCodecContext	   *pCodecCtx;
	AVCodec            *pCodec;
	AVPacket           *dec_pkt, enc_pkt;
	AVFrame            *pframe, *pFrameYUV;
	struct SwsContext  *img_convert_ctx;

	int      videoindex, i;
	int      framecnt = 0;
	int      dec_got_frame,enc_got_frame;
	uint8_t  *out_buffer;
};

void rtmp_data::init_input(const char* device, const char* resolution){
    // 类似于句柄，指向video4linux2
	ifmt=av_find_input_format("video4linux2");
	AVDictionary *options = NULL;
	// 调分辨率和帧率
	av_dict_set(&options, "video_size", resolution ,0);
	av_dict_set(&options, "framerate", "60",0);
	// 用句柄打开设备
	if (avformat_open_input(&ifmt_ctx, device, ifmt, &options) != 0){
		cout << "[ERROR] Couldn't open input" << endl;
		return ;
	}
	// 输入初始化
	if (avformat_find_stream_info(ifmt_ctx, NULL)<0)
	{
		cout << "[ERROR] Couldn't find stream information" << endl;
		return ;
	}
	videoindex = -1;
	for (i = 0; i<ifmt_ctx->nb_streams; i++)
		if (ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoindex = i;
			break;
		}

	// 显示设备信息
    //av_dump_format(ifmt_ctx, 0, device, 0);
	if (videoindex == -1)
	{
		cout << "[ERROR] Couldn't find a video stream" << endl;
		return ;
	}
	// 打开编码器
	if (avcodec_open2(ifmt_ctx->streams[videoindex]->codec, avcodec_find_decoder(ifmt_ctx->streams[videoindex]->codec->codec_id), NULL)<0)
	{
		cout << "[ERROR] Couldn't open decoder" << endl;
		return ;
	}

}

void rtmp_data::init_output(const char* out, const char* speed){
    // 初始化输出
	avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out);
	// 初始化输出编码器
	pCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!pCodec){
		cout << "[ERROR] Couldn't find encoder" << endl;
		return ;
	}
	pCodecCtx = avcodec_alloc_context3(pCodec);
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	pCodecCtx->width = ifmt_ctx->streams[videoindex]->codec->width;
	pCodecCtx->height = ifmt_ctx->streams[videoindex]->codec->height;
	
	pCodecCtx->time_base.num = 1;
	pCodecCtx->time_base.den = 25;
	pCodecCtx->bit_rate = 400000;
	pCodecCtx->gop_size = 250;
	/* Some formats want stream headers to be separate. */
	if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		pCodecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;
 
	pCodecCtx->qmin = 10;
	pCodecCtx->qmax = 51;
	// 可选参数
	pCodecCtx->max_b_frames = 1;
	
	// preset 的参数影响 编码速度和图像质量，从而影响延迟  
	// 可以选择  fast  veryfast  superfast ultrafast
	// tune 参数一定是 zerolatency  立即编码的意思
	AVDictionary *param = 0;
	av_dict_set(&param, "preset", speed, 0);
	av_dict_set(&param, "tune", "zerolatency", 0);

	if (avcodec_open2(pCodecCtx, pCodec,&param) < 0){
		cout << "[ERROR] Faild to open encoder" << endl;
		return ;
	}
 
	// 添加新的流
	video_st = avformat_new_stream(ofmt_ctx, pCodec);
	if (video_st == NULL){
		return ;
	}
	video_st->time_base.num = 1;
	video_st->time_base.den = 25;
	video_st->codec = pCodecCtx;
 
	// 打开输出地址，检查是否可以
	if (avio_open(&ofmt_ctx->pb,out, AVIO_FLAG_READ_WRITE) < 0){
		cout << "[ERROR] Faild to open the output" << endl;
		return ;
	}

    // 显示输出信息
	//av_dump_format(ofmt_ctx, 0, out, 1);

    // 写文件头
	avformat_write_header(ofmt_ctx,NULL);
 
	// 定义，初始化解码用到
	dec_pkt = (AVPacket *)av_malloc(sizeof(AVPacket));
	
	// 将rgb的数据转化从YUV420P
	img_convert_ctx = sws_getContext(ifmt_ctx->streams[videoindex]->codec->width, ifmt_ctx->streams[videoindex]->codec->height, 
		ifmt_ctx->streams[videoindex]->codec->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	
	// 定义好格式以及包括分辨率在内的所有东西
	pFrameYUV = av_frame_alloc();
	pFrameYUV->format = AV_PIX_FMT_YUV420P;
	pFrameYUV->width = 640;
	pFrameYUV->height = 480;

	out_buffer = (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
	
}

void rtmp_data::write_time(){
    // Write PTS
	// 读数据
    AVRational time_base = ofmt_ctx->streams[videoindex]->time_base;
    AVRational r_framerate1 = ifmt_ctx->streams[videoindex]->r_frame_rate;
    AVRational time_base_q = { 1, AV_TIME_BASE };
    // av_q2d()把AVRational结构转换成double，通过这个函数可以计算出某一帧在视频中的时间位置
	// 将实际时间转化为基准时间AV_TIME_BASE
    int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));
    //Parameters
	// av_rescale_q(a,b,c)作用相当于执行a*b/c
    enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
    enc_pkt.dts = enc_pkt.pts;
    enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base);
    enc_pkt.pos = -1;

	framecnt++ ;
}

void rtmp_data::decode_and_encode(int ret){
	int64_t start_time=av_gettime();
	while (av_read_frame(ifmt_ctx, dec_pkt) >= 0){	
		av_log(NULL, AV_LOG_DEBUG, "Reencode the frame\n");
		pframe = av_frame_alloc();
		if (!pframe) {
			ret = AVERROR(ENOMEM);
			return ;
		}
		
		ret = avcodec_decode_video2(ifmt_ctx->streams[dec_pkt->stream_index]->codec, pframe,
			&dec_got_frame, dec_pkt);
		if (ret < 0) {
			av_frame_free(&pframe);
			av_log(NULL, AV_LOG_ERROR, "Fail to Decode\n");
			break;
		}
		if (dec_got_frame){
			sws_scale(img_convert_ctx, (const uint8_t* const*)pframe->data, pframe->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);	
 
			enc_pkt.data = NULL;
			enc_pkt.size = 0;
			av_init_packet(&enc_pkt);
																																																																																								
			ret = avcodec_encode_video2(pCodecCtx, &enc_pkt, pFrameYUV, &enc_got_frame);
			av_frame_free(&pframe);
			if (enc_got_frame == 1){
				enc_pkt.stream_index = video_st->index;
 
				write_time();
 
				ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
				av_free_packet(&enc_pkt);
			}
			
		}
		else {
			av_frame_free(&pframe);
		}
		
		av_free_packet(dec_pkt);
	}
}

int rtmp_data::flush_encoder(){
	int stream_index = 0 ;
	int ret;
	int got_frame;
	AVPacket enc_pkt;
	if (!(ofmt_ctx->streams[stream_index]->codec->codec->capabilities &
		CODEC_CAP_DELAY))
		return 0;
	while (1) {
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
		ret = avcodec_encode_video2 (ofmt_ctx->streams[stream_index]->codec, &enc_pkt,
			NULL, &got_frame);
		av_frame_free(NULL);
		if (ret < 0)
			break;
		if (!got_frame){
			ret=0;
			break;
		}
		printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n",enc_pkt.size);
 
		write_time();
 
		/* copy packet*/
		//转换PTS/DTS（Convert PTS/DTS）
		enc_pkt.pos = -1;
		
		ofmt_ctx->duration=enc_pkt.duration * framecnt;
 
		/* mux encoded frame */
		ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
		if (ret < 0)
			break;
	}
	return ret;
}

void rtmp_data::end_process(){
	// 写文件尾
	av_write_trailer(ofmt_ctx);
 
	// 清除申请的数据空间
	if (video_st)
		avcodec_close(video_st->codec);
	av_free(out_buffer);
	avio_close(ofmt_ctx->pb);
	avformat_free_context(ifmt_ctx);
	avformat_free_context(ofmt_ctx);
}

int main(int argc, char* argv[]){
	const char* out_path_0 = "rtmp://localhost/live/my_video0";
	const char* out_path_1 = "rtmp://localhost/live/my_video2";
	const char* out_path_2 = "rtmp://localhost/live/my_video4";
	
	const char* device_name_0 = "/dev/video0";
	const char* device_name_1 = "/dev/video2";
	const char* device_name_2 = "/dev/video4";
	
	if(argc != 4){
		cout << "Please input four data for this program" << endl;
		return -1;
	}
	else{
		int i, ret;
		
		const char* resolution = argv[2] ;
		const char* code_speed = argv[3] ;
		
		rtmp_data RTMP_data;
	    
		// 初始化
		av_register_all();
		avdevice_register_all();
		avformat_network_init();
		cout << endl << "==============[INFO] Begin Project    ==================" << endl;
		if(strcmp(argv[1],"0") == 0){
			
			RTMP_data.init_input(device_name_0 , resolution);
			RTMP_data.init_output(out_path_0, code_speed);
		}
		else if(strcmp(argv[1],"2") == 0){
			RTMP_data.init_input(device_name_1 , resolution);
			RTMP_data.init_output(out_path_1,code_speed);
		}
		else if(strcmp(argv[1],"4") == 0){
			RTMP_data.init_input(device_name_2 , resolution);
			RTMP_data.init_output(out_path_2, code_speed);
		}
			

		cout << endl << "==============[INFO] Initial Complete ==================" << endl;

		RTMP_data.decode_and_encode(ret);	

		//Flush Encoder
		ret = RTMP_data.flush_encoder();
		if (ret < 0) {
			printf("Flushing encoder failed\n");
			return -1;
		}
		
		RTMP_data.end_process();

		cout << endl << "================[INFO] End of Project =================" << endl;
	}
	return 0;
}

