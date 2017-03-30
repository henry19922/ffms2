//  Copyright (c) 2007-2015 Fredrik Mellbin
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

#ifndef INDEXING_H
#define INDEXING_H

#include "utils.h"

#include <map>
#include <memory>

class Wave64Writer;
class ZipFile;

struct SharedVideoContext {
	AVCodecContext *CodecContext = nullptr;
	AVCodecParserContext *Parser = nullptr;

	~SharedVideoContext();
};

struct SharedAudioContext {
	AVCodecContext *CodecContext = nullptr;
	Wave64Writer *W64Writer = nullptr;
	int64_t CurrentSample = 0;

	~SharedAudioContext();
};

struct FFMS_Index : public std::vector<FFMS_Track> {
	int RefCount = 1;
	FFMS_Index(FFMS_Index const&) = delete;
	FFMS_Index& operator=(FFMS_Index const&) = delete;
	void ReadIndex(ZipFile &zf, const char* IndexFile);
	void WriteIndex(ZipFile &zf);
public:
	static void CalculateFileSignature(const char *Filename, int64_t *Filesize, uint8_t Digest[20]);
	static void CalculateFileSignatureMem(const char *Vid_buf, int64_t buf_len, int64_t *Filesize, uint8_t Digest[20]);

	void AddRef();
	void Release();

	int Decoder;
	int ErrorHandling;
	int64_t Filesize;
	uint8_t Digest[20];

	void Finalize(std::vector<SharedVideoContext> const& video_contexts);
	bool CompareFileSignature(const char *Filename);
	bool CompareFileSignatureMem(const char *Vid_buf, int64_t buf_len);
	void WriteIndexFile(const char *IndexFile);
	uint8_t *WriteIndexBuffer(size_t *Size);

	FFMS_Index(const char *IndexFile);
	FFMS_Index(const uint8_t *Buffer, size_t Size);
	FFMS_Index(int64_t Filesize, uint8_t Digest[20], int Decoder, int ErrorHandling);
};

struct FFMS_Indexer {
	std::map<int, FFMS_AudioProperties> LastAudioProperties;
	FFMS_Indexer(FFMS_Indexer const&) = delete;
	FFMS_Indexer& operator=(FFMS_Indexer const&) = delete;
protected:
	// Index a track if key exists, dump track if value is true
	std::map<int, bool> IndexMask;
	int ErrorHandling = FFMS_IEH_CLEAR_TRACK;
	TIndexCallback IC = nullptr;
	void *ICPrivate = nullptr;
	TAudioNameCallback ANC = nullptr;
	void *ANCPrivate = nullptr;
	std::string SourceFile;
	ScopedFrame DecodeFrame;

	int64_t Filesize;
	uint8_t Digest[20];

	void WriteAudio(SharedAudioContext &AudioContext, FFMS_Index *Index, int Track);
	void CheckAudioProperties(int Track, AVCodecContext *Context);
	uint32_t IndexAudioPacket(int Track, AVPacket *Packet, SharedAudioContext &Context, FFMS_Index &TrackIndices);
	void ParseVideoPacket(SharedVideoContext &VideoContext, AVPacket &pkt, int *RepeatPict, int *FrameType, bool *Invisible);

public:
	FFMS_Indexer(const char *Filename);
	FFMS_Indexer(const char *Vid_Buf, int64_t Buf_Len);
	virtual ~FFMS_Indexer() { }

	void SetIndexTrack(int Track, bool Index, bool Dump);
	void SetIndexTrackType(int TrackType, bool Index, bool Dump);
	void SetErrorHandling(int ErrorHandling);
	void SetProgressCallback(TIndexCallback IC, void *ICPrivate);
	void SetAudioNameCallback(TAudioNameCallback ANC, void *ANCPrivate);

	virtual FFMS_Index *DoIndexing() = 0;
	virtual int GetNumberOfTracks() = 0;
	virtual FFMS_TrackType GetTrackType(int Track) = 0;
	virtual const char *GetTrackCodec(int Track) = 0;
	virtual FFMS_Sources GetSourceType() = 0;
	virtual const char *GetFormatName() = 0;
};

FFMS_Indexer *CreateIndexer(const char *Filename);

FFMS_Indexer *CreateIndexer(const char *VidBuf, int64_t VidLen);

FFMS_Indexer *CreateLavfIndexer(const char *Filename, AVFormatContext *FormatContext);

FFMS_Indexer *CreateLavfIndexer(const char *pszVidBuf, int64_t iVidLen, AVFormatContext *FormatContext);

#endif
