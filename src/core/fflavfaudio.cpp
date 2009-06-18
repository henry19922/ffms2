//  Copyright (c) 2007-2009 Fredrik Mellbin
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "ffaudiosource.h"

void FFLAVFAudio::Free(bool CloseCodec) {
	if (CloseCodec)
		avcodec_close(CodecContext);
	if (FormatContext)
		av_close_input_file(FormatContext);
}

FFLAVFAudio::FFLAVFAudio(const char *SourceFile, int Track, FFIndex *Index, char *ErrorMsg, unsigned MsgSize) {
	FormatContext = NULL;
	AVCodec *Codec = NULL;
	AudioTrack = Track;
	Frames = (*Index)[AudioTrack];

	if (Frames.size() == 0) {
		Free(false);
		snprintf(ErrorMsg, MsgSize, "Audio track contains no frames, was it indexed properly?");
		throw ErrorMsg;
	}

	if (av_open_input_file(&FormatContext, SourceFile, NULL, 0, NULL) != 0) {
		snprintf(ErrorMsg, MsgSize, "Couldn't open '%s'", SourceFile);
		throw ErrorMsg;
	}

	if (av_find_stream_info(FormatContext) < 0) {
		Free(false);
		snprintf(ErrorMsg, MsgSize, "Couldn't find stream information");
		throw ErrorMsg;
	}

	CodecContext = FormatContext->streams[AudioTrack]->codec;

	Codec = avcodec_find_decoder(CodecContext->codec_id);
	if (Codec == NULL) {
		Free(false);
		snprintf(ErrorMsg, MsgSize, "Audio codec not found");
		throw ErrorMsg;
	}

	if (avcodec_open(CodecContext, Codec) < 0) {
		Free(false);
		snprintf(ErrorMsg, MsgSize, "Could not open audio codec");
		throw ErrorMsg;
	}

	// Always try to decode a frame to make sure all required parameters are known
	int64_t Dummy;
	if (DecodeNextAudioBlock(&Dummy, ErrorMsg, MsgSize) < 0) {
		Free(true);
		throw ErrorMsg;
	}
	if (av_seek_frame(FormatContext, AudioTrack, Frames[0].DTS, AVSEEK_FLAG_BACKWARD) < 0)
		av_seek_frame(FormatContext, AudioTrack, Frames[0].DTS, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
	avcodec_flush_buffers(CodecContext);

	FillAP(AP, CodecContext, Frames);

	if (AP.SampleRate <= 0 || AP.BitsPerSample <= 0) {
		Free(true);
		snprintf(ErrorMsg, MsgSize, "Codec returned zero size audio");
		throw ErrorMsg;
	}

	AudioCache.Initialize((AP.Channels * AP.BitsPerSample) / 8, 50);
}

int FFLAVFAudio::DecodeNextAudioBlock(int64_t *Count, char *ErrorMsg, unsigned MsgSize) {
	const size_t SizeConst = (av_get_bits_per_sample_format(CodecContext->sample_fmt) * CodecContext->channels) / 8;
	int Ret = -1;
	*Count = 0;
	uint8_t *Buf = DecodingBuffer;
	AVPacket Packet, TempPacket;
	InitNullPacket(&Packet);
	InitNullPacket(&TempPacket);

	while (av_read_frame(FormatContext, &Packet) >= 0) {
        if (Packet.stream_index == AudioTrack) {
			TempPacket.data = Packet.data;
			TempPacket.size = Packet.size;

			while (TempPacket.size > 0) {
				int TempOutputBufSize = AVCODEC_MAX_AUDIO_FRAME_SIZE * 10;
				Ret = avcodec_decode_audio3(CodecContext, (int16_t *)Buf, &TempOutputBufSize, &TempPacket);

				if (Ret < 0) {// throw error or something?
					av_free_packet(&Packet);
					goto Done;
				}

				if (Ret > 0) {
					TempPacket.size -= Ret;
					TempPacket.data += Ret;
					Buf += TempOutputBufSize;
					if (SizeConst)
						*Count += TempOutputBufSize / SizeConst;
				}
			}

			av_free_packet(&Packet);
			goto Done;
        }

		av_free_packet(&Packet);
	}

Done:
	return Ret;
}

int FFLAVFAudio::GetAudio(void *Buf, int64_t Start, int64_t Count, char *ErrorMsg, unsigned MsgSize) {
	const int64_t SizeConst = (av_get_bits_per_sample_format(CodecContext->sample_fmt) * CodecContext->channels) / 8;
	memset(Buf, 0, static_cast<size_t>(SizeConst * Count));

	int PreDecBlocks = 0;
	uint8_t *DstBuf = static_cast<uint8_t *>(Buf);

	// Fill with everything in the cache
	int64_t CacheEnd = AudioCache.FillRequest(Start, Count, DstBuf);
	// Was everything in the cache?
	if (CacheEnd == Start + Count)
		return 0;

	size_t CurrentAudioBlock;
	// Is seeking required to decode the requested samples?
//	if (!(CurrentSample >= Start && CurrentSample <= CacheEnd)) {
	if (CurrentSample != CacheEnd) {
		PreDecBlocks = 15;
		CurrentAudioBlock = FFMAX((int64_t)Frames.FindClosestAudioKeyFrame(CacheEnd) - PreDecBlocks - 20, (int64_t)0);

		// Did the seeking fail?
		if (av_seek_frame(FormatContext, AudioTrack, Frames[CurrentAudioBlock].DTS, AVSEEK_FLAG_BACKWARD) < 0)
			av_seek_frame(FormatContext, AudioTrack, Frames[CurrentAudioBlock].DTS, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);

		avcodec_flush_buffers(CodecContext);

		AVPacket Packet;
		InitNullPacket(&Packet);

		// Establish where we actually are
		// Trigger on packet dts difference since groups can otherwise be indistinguishable
		int64_t LastDTS = - 1;
		while (av_read_frame(FormatContext, &Packet) >= 0) {
			if (Packet.stream_index == AudioTrack) {
				if (LastDTS < 0) {
					LastDTS = Packet.dts;
				} else if (LastDTS != Packet.dts) {
					for (size_t i = 0; i < Frames.size(); i++)
						if (Frames[i].DTS == Packet.dts) {
							// The current match was consumed
							CurrentAudioBlock = i + 1;
							break;
						}

					av_free_packet(&Packet);
					break;
				}
			}

			av_free_packet(&Packet);
		}
	} else {
		CurrentAudioBlock = Frames.FindClosestAudioKeyFrame(CurrentSample);
	}

	int64_t DecodeCount;

	do {
		int Ret = DecodeNextAudioBlock(&DecodeCount, ErrorMsg, MsgSize);
		if (Ret < 0) {
			// FIXME
			//Env->ThrowError("Bleh, bad audio decoding");
		}

		// Cache the block if enough blocks before it have been decoded to avoid garbage
		if (PreDecBlocks == 0) {
			AudioCache.CacheBlock(Frames[CurrentAudioBlock].SampleStart, DecodeCount, DecodingBuffer);
			CacheEnd = AudioCache.FillRequest(CacheEnd, Start + Count - CacheEnd, DstBuf + (CacheEnd - Start) * SizeConst);
		} else {
			PreDecBlocks--;
		}

		CurrentAudioBlock++;
		if (CurrentAudioBlock < Frames.size())
			CurrentSample = Frames[CurrentAudioBlock].SampleStart;
	} while (Start + Count - CacheEnd > 0 && CurrentAudioBlock < Frames.size());

	return 0;
}

FFLAVFAudio::~FFLAVFAudio() {
	Free(true);
}