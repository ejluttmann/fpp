/*
 *   VirtualDisplay Channel Output for Falcon Player (FPP)
 *
 *   Copyright (C) 2015 the Falcon Player Developers
 *      Initial development by:
 *      - David Pitts (dpitts)
 *      - Tony Mace (MyKroFt)
 *      - Mathew Mrosko (Materdaddy)
 *      - Chris Pinkham (CaptainMurdoch)
 *      For additional credits and developers, see credits.php.
 *
 *   The Falcon Player (FPP) is free software; you can redistribute it
 *   and/or modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "common.h"
#include "log.h"
#include "VirtualDisplay.h"
#include "Sequence.h"
#include "settings.h"

/////////////////////////////////////////////////////////////////////////////
// To disable interpolated scaling on the GPU, add this to /boot/config.txt:
// scaling_kernel=8

/*
 *
 */
VirtualDisplayOutput::VirtualDisplayOutput(unsigned int startChannel,
	unsigned int channelCount)
  : ChannelOutputBase(startChannel, channelCount),
	m_width(1280),
	m_height(1024),
	m_scale(1.0),
	m_previewWidth(800),
	m_previewHeight(600),
	m_virtualDisplay(NULL),
	m_pixelSize(2)
{
	LogDebug(VB_CHANNELOUT, "VirtualDisplayOutput::VirtualDisplayOutput(%u, %u)\n",
		startChannel, channelCount);

	m_maxChannels = FPPD_MAX_CHANNELS;
}

/*
 *
 */
VirtualDisplayOutput::~VirtualDisplayOutput()
{
	LogDebug(VB_CHANNELOUT, "VirtualDisplayOutput::~VirtualDisplayOutput()\n");
}

/*
 *
 */
int VirtualDisplayOutput::Init(Json::Value config)
{
	LogDebug(VB_CHANNELOUT, "VirtualDisplayOutput::Init()\n");

	m_width = config["width"].asInt();
	if (!m_width)
		m_width = 1280;

	m_height = config["height"].asInt();
	if (!m_height)
		m_height = 1024;

	m_pixelSize = config["pixelSize"].asInt();
	if (!m_pixelSize)
		m_pixelSize = 2;

	if (config.isMember("colorOrder"))
		m_colorOrder = config["colorOrder"].asString();

	std::string virtualDisplayMapFilename(getMediaDirectory());
	virtualDisplayMapFilename += "/config/virtualdisplaymap";

	if (!FileExists(virtualDisplayMapFilename.c_str()))
	{
		LogErr(VB_CHANNELOUT, "Error: %s does not exist\n",
			virtualDisplayMapFilename.c_str());
		return 0;
	}

	FILE *file = fopen(virtualDisplayMapFilename.c_str(), "r");

	if (file == NULL)
	{
		LogErr(VB_CHANNELOUT, "Error: unable to open %s: %s\n",
			virtualDisplayMapFilename.c_str(), strerror(errno));
		return 0;
	}

	char * line = NULL;
	size_t len = 0;
	ssize_t read;

	std::string pixel;
	std::vector<std::string> parts;
	int x = 0;
	int y = 0;
	int ch = 0;
	int s = 0;
	int r = 0;
	int g = 0;
	int b = 0;
	int first = 1;

	while ((read = getline(&line, &len, file)) != -1)
	{
		if (( ! line ) || ( ! read ) || ( read == 1 ))
			continue;

		pixel = line;
		parts = split(pixel, ',');

		if (first)
		{
			if (parts.size() == 2)
			{
				m_previewWidth  = atoi(parts[0].c_str());
				m_previewHeight = atoi(parts[1].c_str());

				if ((1.0 * m_width / m_previewWidth) > (1.0 * m_height / m_previewHeight))
				{
					m_scale = 1.0 * m_height / m_previewHeight;
				}
				else
				{
					m_scale = 1.0 * m_width / m_previewWidth;
				}
			}

			first = 0;
			continue;
		}

		if (parts.size() == 3)
		{
			x  = atoi(parts[0].c_str());
			y  = atoi(parts[1].c_str());
			ch = atoi(parts[2].c_str());

			s = ((m_height - (int)(y * m_scale) - 1) * m_width
					+ (int)(x * m_scale)) * m_bytesPerPixel;

			if (m_colorOrder == "RGB")
			{
				r = s + 0;
				g = s + 1;
				b = s + 2;
			}
			else if (m_colorOrder == "RBG")
			{
				r = s + 0;
				g = s + 2;
				b = s + 1;
			}
			else if (m_colorOrder == "GRB")
			{
				r = s + 1;
				g = s + 0;
				b = s + 2;
			}
			else if (m_colorOrder == "GBR")
			{
				r = s + 2;
				g = s + 0;
				b = s + 1;
			}
			else if (m_colorOrder == "BRG")
			{
				r = s + 1;
				g = s + 2;
				b = s + 0;
			}
			else if (m_colorOrder == "BGR")
			{
				r = s + 2;
				g = s + 1;
				b = s + 0;
			}

			VirtualDisplayPixel p = { x, y, ch, r, g, b };
			m_pixels.push_back(p);
		}
	}

	return ChannelOutputBase::Init(config);
}


/*
 *
 */
void VirtualDisplayOutput::DrawPixels(unsigned char *channelData)
{
	char *c = (char *)channelData;
	char r = 0;
	char g = 0;
	char b = 0;
	int stride = m_width * m_bytesPerPixel;
	VirtualDisplayPixel pixel;

	for (int i = 0; i < m_pixels.size(); i++)
	{
		pixel = m_pixels[i];

		if (m_pixelSize == 2)
		{
			r = channelData[pixel.ch    ];
			g = channelData[pixel.ch + 1];
			b = channelData[pixel.ch + 2];

			m_virtualDisplay[pixel.r] = r;
			m_virtualDisplay[pixel.g] = g;
			m_virtualDisplay[pixel.b] = b;

			r /= 2;
			g /= 2;
			b /= 2;

			m_virtualDisplay[pixel.r + m_bytesPerPixel] = r;
			m_virtualDisplay[pixel.g + m_bytesPerPixel] = g;
			m_virtualDisplay[pixel.b + m_bytesPerPixel] = b;

			m_virtualDisplay[pixel.r - m_bytesPerPixel] = r;
			m_virtualDisplay[pixel.g - m_bytesPerPixel] = g;
			m_virtualDisplay[pixel.b - m_bytesPerPixel] = b;

			m_virtualDisplay[pixel.r + stride] = r;
			m_virtualDisplay[pixel.g + stride] = g;
			m_virtualDisplay[pixel.b + stride] = b;

			m_virtualDisplay[pixel.r - stride] = r;
			m_virtualDisplay[pixel.g - stride] = g;
			m_virtualDisplay[pixel.b - stride] = b;
		}
		else
		{
			m_virtualDisplay[pixel.r] = channelData[pixel.ch    ];
			m_virtualDisplay[pixel.g] = channelData[pixel.ch + 1];
			m_virtualDisplay[pixel.b] = channelData[pixel.ch + 2];
		}
	}
}


/*
 *
 */
void VirtualDisplayOutput::DumpConfig(void)
{
	LogDebug(VB_CHANNELOUT, "VirtualDisplayOutput::DumpConfig()\n");
	LogDebug(VB_CHANNELOUT, "    width         : %d\n", m_width);
	LogDebug(VB_CHANNELOUT, "    height        : %d\n", m_height);
	LogDebug(VB_CHANNELOUT, "    scale         : %0.3f\n", m_scale);
	LogDebug(VB_CHANNELOUT, "    preview width : %d\n", m_previewWidth);
	LogDebug(VB_CHANNELOUT, "    preview height: %d\n", m_previewHeight);
	LogDebug(VB_CHANNELOUT, "    color Order   : %s\n", m_colorOrder.c_str());
	LogDebug(VB_CHANNELOUT, "    pixel count   : %d\n", m_pixels.size());
	LogDebug(VB_CHANNELOUT, "    pixel size    : %d\n", m_pixelSize);
}

