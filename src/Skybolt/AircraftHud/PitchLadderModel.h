/* Copyright 2012-2020 Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */


#pragma once

#include "HudDrawer.h"

namespace skybolt {

class PitchLadderModel
{
public:
	struct Parameters
	{
		void makeDefault()
		{
			pitchAngleIncrement = 10.f * math::degToRadF();
			pitchGapHeight = 0.05f;
			lineWidth = 0.3f;
			wingletHeight = 0.01f;
			textOffset = 0.03f;
			maxPitchAngle = math::halfPiF();
		}

		float pitchAngleIncrement;
		float pitchGapHeight;
		float lineWidth;
		float wingletHeight;
		float textOffset;
		float maxPitchAngle;
	};

	PitchLadderModel(HudDrawer* drawer, const Parameters& param);
	void draw(float pitch, float roll);

	void setPitchGapHeight(double height) { mParam.pitchGapHeight = height; }

protected:
	void drawRung(float rungPitch, float pitch, float roll, float width, const HudDrawer::DashedLineParams* params = 0);
	void drawHalfRung(float relY, float rungPitch, float roll, float signedWidth, const HudDrawer::DashedLineParams* params = 0);

private:

	HudDrawer* mDrawer;
	Parameters mParam;
	HudDrawer::DashedLineParams mDashedLineParams;
};

} // namespace skybolt
