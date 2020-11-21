/* Copyright 2012-2020 Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "EngineRootFactory.h"
#include "EngineCommandLineParser.h"
#include "GetExecutablePath.h"
#include "Plugin/PluginHelpers.h"
#include <SkyboltCommon/OptionalUtility.h>

#include <boost/log/trivial.hpp>

using namespace nlohmann;

namespace skybolt {

template <typename T>
T getOptionalNodeOrDefaultWithWarning(const json& j, const std::string& name, const T& defaultValue)
{
	auto i = j.find(name);
	if (i != j.end())
	{
		return i.value().get<T>();
	}
	BOOST_LOG_TRIVIAL(warning) << "Missing parameter '" << name << "'. Default value of '" << defaultValue << "' will be used.";
	return defaultValue;
}

std::unique_ptr<EngineRoot> EngineRootFactory::create(const boost::program_options::variables_map& params)
{
	nlohmann::json settings = skybolt::optionalMapOrElse<nlohmann::json>(EngineCommandLineParser::readSettings(params), [] { return nlohmann::json(); });

	std::string pluginsDir = getExecutablePath().append("plugins").string();
	std::vector<PluginFactory> enginePluginFactories = loadPluginFactories<Plugin, PluginConfig>(pluginsDir);

	return create(enginePluginFactories, settings);
}

std::unique_ptr<EngineRoot> EngineRootFactory::create(const std::vector<PluginFactory>& pluginFactories, const json& settings)
{
	EngineRootConfig config;
	config.pluginFactories = pluginFactories;
	config.tileSourceFactoryConfig.bingApiKey = getOptionalNodeOrDefaultWithWarning(settings, "bingApiKey", std::string());
	config.tileSourceFactoryConfig.cacheDirectory = "Cache";
	return std::make_unique<EngineRoot>(config);
}

} // namespace skybolt