/* Copyright 2012-2020 Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include "TileImagesLoader.h"

namespace skybolt {
namespace vis {

struct PlanetTileImages : TileImages
{
	~PlanetTileImages() override = default;

	TileImage heightMapImage;
	osg::ref_ptr<osg::Image> normalMapImage; //!< Same tile key as heightMapImage
	osg::ref_ptr<osg::Image> landMaskImage; //!< Same tile key as heightMapImage

	TileImage albedoMapImage;
	std::optional<TileImage> attributeMapImage;
};

class PlanetTileImagesLoader : public TileImagesLoader
{
public:
	TileSourcePtr elevationLayer; //!< never null
	TileSourcePtr attributeLayer; //!< can be null
	TileSourcePtr albedoLayer; //!< never null
	int maxElevationLod;
	int minAttributeLod; //!< Load attribute tiles for lod levels of at least this
	int maxAttributeLod;

	enum class CacheIndex
	{
		Elevation,
		LandMask,
		Albedo,
		Attribute
	};

	PlanetTileImagesLoader(double planetRadius) : TileImagesLoader(4), mPlanetRadius(planetRadius) {}

	//! May be called from multiple threads
	TileImagesPtr load(const skybolt::QuadTreeTileKey& key, std::function<bool()> cancelSupplier) const override;

private:
	const double mPlanetRadius;
};

} // namespace vis
} // namespace skybolt
