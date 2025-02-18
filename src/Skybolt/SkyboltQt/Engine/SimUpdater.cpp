/* Copyright Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "SimUpdater.h"
#include <SkyboltEngine/EngineRoot.h>
#include <SkyboltSim/System/SimStepper.h>

using namespace skybolt;
using namespace skybolt::sim;

const SecondsD maxWallDt = 0.2;

SimUpdater::SimUpdater(const std::shared_ptr<skybolt::EngineRoot>& engineRoot) :
	mEngineRoot(engineRoot),
	mSimStepper(std::make_unique<SimStepper>(engineRoot->systemRegistry)),
	mAverageWallDt(std::make_unique<UniformAveragedBuffer>(16))
{
	mSimStepper->setMaxDynamicsSubsteps(std::nullopt);
}

SimUpdater::~SimUpdater() = default;

void SimUpdater::update(SecondsD wallDt)
{
	if (wallDt <= 0)
	{
		return;
	}

	mSimStepper->setDynamicsEnabled(mEngineRoot->scenario->timelineMode.get() == TimelineMode::Live);
	
	// Set SimStepper to the current time.
	// This is necessary because the current time may have changed since the last update, for example if we jumped to a different point on the timeline.
	TimeSource& timeSource = mEngineRoot->scenario->timeSource;
	SecondsD simTime = timeSource.getTime();
	mSimStepper->setTime(simTime);

	// Calculate simDt
	double simDt;
	if (timeSource.getState() == TimeSource::StatePlaying)
	{
		mAverageWallDt->addValue(wallDt);
		double averageWallDt = mAverageWallDt->getResult();

		double maxTimeRate = mActualTimeRate * maxWallDt / averageWallDt;
		mActualTimeRate = std::min(mRequestedTimeRate, maxTimeRate);
		simDt = std::min(averageWallDt * mActualTimeRate, mMaxSimDt);
		mActualTimeRate = simDt / averageWallDt;
	}
	else
	{
		simDt = 0;
	}

	// Advance time
	simulate(timeSource, simDt);

	for (const SystemPtr& system : *mEngineRoot->systemRegistry)
	{
		system->advanceWallTime(mWallTime, wallDt);
	}

	mWallTime += wallDt;
}

void SimUpdater::simulate(TimeSource& timeSource, float dt)
{
	SecondsD prevSimTime = timeSource.getTime();
	timeSource.advanceTime(dt);
	
	double dtSim = std::max(0.0, timeSource.getTime() - prevSimTime);
	mSimStepper->update(dtSim);
}
