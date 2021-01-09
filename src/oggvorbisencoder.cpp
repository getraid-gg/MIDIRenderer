#include "oggvorbisencoder.h"

#include "platformsupport.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sstream>

#include <vorbis/vorbisenc.h>

OggVorbisEncoder::OggVorbisEncoder(int streamID, long sampleRate, float quality) : m_isComplete(false), m_hasWrittenHeaders(false),
	m_overlapOffset(0)
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

	ogg_stream_init(&m_stream, streamID);
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

bool OggVorbisEncoder::getHasWrittenHeaders() { return m_hasWrittenHeaders; }

void OggVorbisEncoder::addComment(std::string tag, std::string contents)
{
	throwIfWrittenHeaders();
	vorbis_comment_add_tag(&m_comment, tag.c_str(), contents.c_str());
}

void OggVorbisEncoder::writeBuffers(float* leftBuffer, float* rightBuffer, size_t frameCount)
{
	throwIfComplete();
	size_t sourceFrameOffset = 0;
	
	const size_t bufferSize = m_rawBuffer[0].size();

	while (m_overlapOffset > 0 && sourceFrameOffset < frameCount)
	{
		m_rawBuffer[0][bufferSize - m_overlapOffset] += leftBuffer[sourceFrameOffset];
		m_rawBuffer[1][bufferSize - m_overlapOffset] += rightBuffer[sourceFrameOffset];
		m_overlapOffset--;
		sourceFrameOffset++;
	}

	if (sourceFrameOffset < frameCount)
	{
		m_rawBuffer[0].insert(m_rawBuffer[0].end(), &leftBuffer[sourceFrameOffset], &leftBuffer[frameCount]);
		m_rawBuffer[1].insert(m_rawBuffer[1].end(), &rightBuffer[sourceFrameOffset], &rightBuffer[frameCount]);
	}
}

void OggVorbisEncoder::setOverlapOffset(size_t offset)
{
	if (m_overlapOffset > m_rawBuffer[0].size())
	{
		throw std::out_of_range("Ogg Vorbis file write overlap offset is greater than the number of samples already written");
	}

	m_overlapOffset = offset;
}

void OggVorbisEncoder::writeToFile(std::string filePath)
{
	throwIfComplete();

	m_isComplete = true;
	m_hasWrittenHeaders = true;

	ogg_packet header;
	ogg_packet commentsHeader;
	ogg_packet codebookHeader;

	vorbis_analysis_headerout(&m_dspState, &m_comment, &header, &commentsHeader, &codebookHeader);
	ogg_stream_packetin(&m_stream, &header);
	ogg_stream_packetin(&m_stream, &commentsHeader);
	ogg_stream_packetin(&m_stream, &codebookHeader);

	std::ofstream outputFileStream;
	outputFileStream.open(midirenderer::stringutils::getPlatformString(filePath), std::ios_base::out | std::ios_base::binary);

	if (!outputFileStream.is_open())
	{
		std::stringstream errorStringStream;
		errorStringStream << "Failed to open file path " << filePath << " for writing";
		throw std::runtime_error(errorStringStream.str().c_str());
	}

	// flush the stream to end the header page before starting the audio page (required by the Vorbis spec)
	while (true)
	{
		int streamFlushLength = ogg_stream_flush(&m_stream, &m_page);
		if (streamFlushLength == 0) { break; }
		outputFileStream.write(reinterpret_cast<const char*>(m_page.header), m_page.header_len);
		outputFileStream.write(reinterpret_cast<const char*>(m_page.body), m_page.body_len);
	}

	int offset = 0;
	int framesRemaining = m_rawBuffer[0].size();
	while (framesRemaining > 0)
	{
		int frameCount = std::min(s_writeChunkSize, framesRemaining);

		float** buffer = vorbis_analysis_buffer(&m_dspState, frameCount);

		std::copy(m_rawBuffer[0].begin() + offset, m_rawBuffer[0].begin() + offset + frameCount, buffer[0]);
		std::copy(m_rawBuffer[1].begin() + offset, m_rawBuffer[1].begin() + offset + frameCount, buffer[1]);

		vorbis_analysis_wrote(&m_dspState, frameCount);

		framesRemaining -= frameCount;
		offset += frameCount;
		if (framesRemaining == 0)
		{
			vorbis_analysis_wrote(&m_dspState, 0);
		}

		while (true)
		{
			int blockStatus = vorbis_analysis_blockout(&m_dspState, &m_block);
			if (blockStatus == 0) { break; }
			else if (blockStatus < 0)
			{
				throw std::runtime_error("Failed to read an audio block while writing file");
			}

			// This is only used when using bitrate management but it's
			// considered good practice even when not using bitrate management
			// https://xiph.org/vorbis/doc/libvorbis/vorbis_analysis.html
			vorbis_analysis(&m_block, nullptr);
			vorbis_bitrate_addblock(&m_block);

			while (true)
			{
				int packetStatus = vorbis_bitrate_flushpacket(&m_dspState, &m_packet);
				if (packetStatus == 0) { break; }
				else if (packetStatus < 0)
				{
					throw std::runtime_error("Failed to read a packet audio block while writing file");
				}

				ogg_stream_packetin(&m_stream, &m_packet);

				while (true)
				{
					int pageStatus = ogg_stream_pageout(&m_stream, &m_page);
					if (pageStatus == 0) { break; }
					else if (pageStatus < 0)
					{
						throw std::runtime_error("Failed to read the stream page out while writing file");
					}

					outputFileStream.write(reinterpret_cast<char*>(m_page.header), m_page.header_len);
					outputFileStream.write(reinterpret_cast<char*>(m_page.body), m_page.body_len);
				}
			}
		}
	}

	outputFileStream.close();
}

void OggVorbisEncoder::throwIfComplete()
{
	if (m_isComplete) { throw std::runtime_error("Attempted to use a completed Vorbis encoder"); }
}

void OggVorbisEncoder::throwIfWrittenHeaders()
{
	if (m_hasWrittenHeaders) { throw std::runtime_error("Attempted to modify headers that have already been written"); }
}
