#pragma once
#include "platformchar.h"

#include <memory>
#include <vector>

#include <vorbis/codec.h>

class OggVorbisEncoder
{
public:
	OggVorbisEncoder(int streamID, long sampleRate, float quality);
	~OggVorbisEncoder();

	bool getIsComplete();
	bool getHasWrittenHeaders();

	void addComment(std::string tag, std::string contents);

	void writeBuffers(float* leftBuffer, float* rightBuffer, size_t frameCount);
	void setOverlapOffset(size_t offset);

	void writeToFile(std::string filePath);

private:
	void throwIfComplete();
	void throwIfWrittenHeaders();

	bool m_isComplete;
	bool m_hasWrittenHeaders;
	vorbis_info m_info;
	vorbis_comment m_comment;
	vorbis_dsp_state m_dspState;
	vorbis_block m_block;
	ogg_stream_state m_stream;

	ogg_page m_page;
	ogg_packet m_packet;

	std::vector<float> m_rawBuffer[2];
	size_t m_overlapOffset;

	constexpr static int s_writeChunkSize = 1024;
};
