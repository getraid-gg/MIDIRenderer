#pragma once
#include <memory>
#include <array>
#include <functional>

#include <vorbis/codec.h>

class OggVorbisEncoder
{
public:
	typedef std::function<void(const unsigned char*, long, const unsigned char*, long)> PageCallbackFunc;

	OggVorbisEncoder(int streamID, long sampleRate, float quality);
	~OggVorbisEncoder();

	bool getIsComplete();

	void addComment(std::string tag, std::string contents);

	void writeBuffers(const float* leftBuffer, const float* rightBuffer, size_t frameCount);
	void startOverlapRegion();
	void endOverlapRegion();

	void readHeader(const PageCallbackFunc& pageCallback);
	void readStreamPages(const PageCallbackFunc& pageCallback);
	void completeStream(const PageCallbackFunc& pageCallback);

private:
	static void executePageCallback(const PageCallbackFunc& pageCallback, const ogg_page& page);
	void encodeBuffers(const float* leftBuffer, const float* rightBuffer, size_t frameCount);

	void throwIfComplete();

	bool m_isComplete;

	int m_streamID;
	vorbis_info m_info;
	vorbis_comment m_comment;
	vorbis_dsp_state m_dspState;
	vorbis_block m_block;
	ogg_stream_state m_stream;

	std::array<std::vector<float>, 2> m_overlapBuffers;
	bool m_isWritingOverlapRegion;
	size_t m_overlapOffset;

	constexpr static size_t s_writeChunkSize = 1024;
};
