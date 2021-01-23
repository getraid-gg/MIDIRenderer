#include "oggvorbisencoder.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sstream>

#include <vorbis/vorbisenc.h>

OggVorbisEncoder::OggVorbisEncoder(int streamID, long sampleRate, float quality) : m_isComplete(false),
	m_streamID(streamID), m_isWritingOverlapRegion(false), m_overlapOffset(0)
{
	vorbis_info_init(&m_info);
	int status = vorbis_encode_init_vbr(&m_info, 2, sampleRate, quality);
	if (status != 0)
	{
		throw std::invalid_argument("Invalid vorbis bitrate or quality");
	}
	vorbis_comment_init(&m_comment);

	vorbis_analysis_init(&m_dspState, &m_info);
	vorbis_block_init(&m_dspState, &m_block);

	ogg_stream_init(&m_stream, m_streamID);
}

OggVorbisEncoder::~OggVorbisEncoder()
{
	ogg_stream_clear(&m_stream);
	vorbis_block_clear(&m_block);
	vorbis_dsp_clear(&m_dspState);
	vorbis_comment_clear(&m_comment);
	vorbis_info_clear(&m_info);
}

bool OggVorbisEncoder::getIsComplete() { return m_isComplete; }

void OggVorbisEncoder::addComment(std::string tag, std::string contents)
{
	vorbis_comment_add_tag(&m_comment, tag.c_str(), contents.c_str());
}

void OggVorbisEncoder::writeBuffers(const float* leftBuffer, const float* rightBuffer, size_t frameCount)
{
	throwIfComplete();

	if (m_isWritingOverlapRegion)
	{
		m_overlapBuffers[0].insert(m_overlapBuffers[0].end(), &leftBuffer[0], &leftBuffer[frameCount]);
		m_overlapBuffers[1].insert(m_overlapBuffers[1].end(), &rightBuffer[0], &rightBuffer[frameCount]);
		m_overlapOffset += frameCount;
	}
	else
	{
		size_t sourceFrameOffset = 0;
		const size_t overlapBufferSize = m_overlapBuffers[0].size();

		while (m_overlapOffset > 0 && sourceFrameOffset < frameCount)
		{
			m_overlapBuffers[0][overlapBufferSize - m_overlapOffset] += leftBuffer[sourceFrameOffset];
			m_overlapBuffers[1][overlapBufferSize - m_overlapOffset] += rightBuffer[sourceFrameOffset];
			m_overlapOffset--;
			sourceFrameOffset++;
		}

		if (m_overlapOffset == 0 && sourceFrameOffset > 0)
		{
			// Exhausted the buffer overlap - it can be written now
			encodeBuffers(m_overlapBuffers[0].data(), m_overlapBuffers[1].data(), overlapBufferSize);
			m_overlapBuffers[0].clear();
			m_overlapBuffers[1].clear();
		}

		if (sourceFrameOffset < frameCount)
		{
			encodeBuffers(&leftBuffer[sourceFrameOffset], &rightBuffer[sourceFrameOffset], frameCount - sourceFrameOffset);
		}
	}
}

void OggVorbisEncoder::startOverlapRegion()
{
	m_isWritingOverlapRegion = true;
}

void OggVorbisEncoder::endOverlapRegion()
{
	m_isWritingOverlapRegion = false;
}

void OggVorbisEncoder::readHeader(const PageCallbackFunc& pageCallback)
{
	ogg_stream_state headerStream;
	ogg_stream_init(&headerStream, m_streamID);

	ogg_packet header;
	ogg_packet commentsHeader;
	ogg_packet codebookHeader;

	vorbis_analysis_headerout(&m_dspState, &m_comment, &header, &commentsHeader, &codebookHeader);
	ogg_stream_packetin(&headerStream, &header);
	ogg_stream_packetin(&headerStream, &commentsHeader);
	ogg_stream_packetin(&headerStream, &codebookHeader);

	ogg_page page;
	while (ogg_stream_flush(&headerStream, &page) != 0)
	{
		executePageCallback(pageCallback, page);
	}

	ogg_stream_clear(&headerStream);
}

void OggVorbisEncoder::readStreamPages(const PageCallbackFunc& pageCallback)
{
	ogg_page page;
	while (ogg_stream_pageout(&m_stream, &page) != 0)
	{
		executePageCallback(pageCallback, page);
	}
}

void OggVorbisEncoder::completeStream(const PageCallbackFunc& pageCallback)
{
	throwIfComplete();
	m_isComplete = true;
	m_isWritingOverlapRegion = false;

	if (m_overlapOffset > 0)
	{
		encodeBuffers(m_overlapBuffers[0].data(), m_overlapBuffers[1].data(), m_overlapBuffers[0].size());
		m_overlapBuffers[0].clear();
		m_overlapBuffers[1].clear();
		m_overlapOffset = 0;
	}

	vorbis_analysis_wrote(&m_dspState, 0);

	flushBufferToStream();

	readStreamPages(pageCallback);
}

void OggVorbisEncoder::executePageCallback(const PageCallbackFunc& pageCallback, const ogg_page& page)
{
	pageCallback(page.header, page.header_len, page.body, page.body_len);
}

void OggVorbisEncoder::encodeBuffers(const float* leftBuffer, const float* rightBuffer, size_t frameCount)
{
	float** buffer = vorbis_analysis_buffer(&m_dspState, frameCount);

	std::copy(leftBuffer, &leftBuffer[frameCount], buffer[0]);
	std::copy(rightBuffer, &rightBuffer[frameCount], buffer[1]);

	vorbis_analysis_wrote(&m_dspState, frameCount);

	flushBufferToStream();
}

void OggVorbisEncoder::flushBufferToStream()
{
	while (true)
	{
		int blockStatus = vorbis_analysis_blockout(&m_dspState, &m_block);
		if (blockStatus == 0) { break; }
		else if (blockStatus < 0)
		{
			throw std::runtime_error("Failed to read an audio block while encoding");
		}

		// This is only used when using bitrate management but it's
		// considered good practice even when not using bitrate management
		// https://xiph.org/vorbis/doc/libvorbis/vorbis_analysis.html
		vorbis_analysis(&m_block, nullptr);
		vorbis_bitrate_addblock(&m_block);

		while (true)
		{
			ogg_packet packet;
			int packetStatus = vorbis_bitrate_flushpacket(&m_dspState, &packet);
			if (packetStatus == 0) { break; }
			else if (packetStatus < 0)
			{
				throw std::runtime_error("Failed to read a packet audio block while encoding");
			}

			ogg_stream_packetin(&m_stream, &packet);
		}
	}
}

void OggVorbisEncoder::throwIfComplete()
{
	if (m_isComplete) { throw std::runtime_error("Attempted to use a completed Vorbis encoder"); }
}