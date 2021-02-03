/* Copyright 2012-2020 Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "EntityFactory.h"
#include "EngineStats.h"
#include "TemplateNameComponent.h"
#include "VisObjectsComponent.h"
#include "SimVisBinding/SimVisBinding.h"
#include "SimVisBinding/GeocentricToNedConverter.h"
#include "SimVisBinding/CameraSimVisBinding.h"
#include "SimVisBinding/MoonVisBinding.h"
#include "SimVisBinding/PlanetVisBinding.h"
#include "SimVisBinding/CelestialObjectVisBinding.h"
#include "SimVisBinding/PolylineVisBinding.h"
#include "SimVisBinding/WakeBinding.h"

#include <SkyboltSim/JsonHelpers.h>
#include <SkyboltSim/World.h>
#include <SkyboltSim/Components/DynamicBodyComponent.h>
#include <SkyboltSim/Components/CameraComponent.h>
#include <SkyboltSim/Components/MainRotorComponent.h>
#include <SkyboltSim/Components/NameComponent.h>
#include <SkyboltSim/Components/Node.h>
#include <SkyboltSim/Components/PlanetComponent.h>
#include <SkyboltSim/Components/PropellerComponent.h>
#include <SkyboltSim/Physics/Astronomy.h>
#include <SkyboltSim/Spatial/GreatCircle.h>

#include <SkyboltVis/Camera.h>
#include <SkyboltVis/Light.h>
#include <SkyboltVis/OsgImageHelpers.h>
#include <SkyboltVis/OsgStateSetHelpers.h>
#include <SkyboltVis/Scene.h>
#include <SkyboltVis/ShaderProgramRegistry.h>
#include <SkyboltVis/ElevationProvider/TilePlanetAltitudeProvider.h>
#include <SkyboltVis/Renderable/Atmosphere/Bruneton/BruentonAtmosphere.h>
#include <SkyboltVis/Renderable/CameraRelativeBillboard.h>
#include <SkyboltVis/Renderable/Polyline.h>
#include <SkyboltVis/Renderable/Planet/Planet.h>
#include <SkyboltVis/Renderable/Planet/Tile/TileSource/JsonTileSourceFactory.h>
#include <SkyboltVis/Renderable/Stars/Starfield.h>
#include <SkyboltVis/Renderable/Model/Model.h>
#include <SkyboltVis/Renderable/Model/ModelFactory.h>

#include <SkyboltCommon/StringVector.h>
#include <SkyboltCommon/File/FileUtility.h>
#include <SkyboltCommon/Json/JsonHelpers.h>
#include <SkyboltCommon/Json/ReadJsonFile.h>
#include <SkyboltCommon/Math/MathUtility.h>

#include <boost/filesystem.hpp>

#include <osg/BlendFunc>
#include <osg/Depth>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osgDB/ReadFile>
#include <osgDB/Registry>

using namespace skybolt;
using namespace skybolt::sim;

class MainRotorVisComponent : public SimVisBinding
{
public:
	MainRotorVisComponent(MainRotorComponent* rotor, const Positionable* attachedBody, const vis::RootNodePtr& visObject) :
		mRotor(rotor),
		mAttachedBody(attachedBody),
		mVisObject(visObject)
	{
		assert(mAttachedBody);
	}

	void syncVis(const GeocentricToNedConverter& converter)
	{
		Vector3 pos = mAttachedBody->getPosition() + mAttachedBody->getOrientation() * mRotor->getPositionRelBody();

		mVisObject->setPosition(converter.convertPosition(pos));
		mVisObject->setOrientation(osg::Quat(mRotor->getRotationAngle(), osg::Vec3f(0, 0, 1)) * converter.convert(mAttachedBody->getOrientation() * mRotor->getTppOrientationRelBody()));
	}

private:
	MainRotorComponent* mRotor;
	const Positionable* mAttachedBody;
	vis::RootNodePtr mVisObject;
};

class PropellerVisComponent : public SimVisBinding
{
public:
	PropellerVisComponent(const PropellerComponent* propeller, const Positionable* attachedBody, const vis::RootNodePtr& visObject) :
		mPropeller(propeller),
		mAttachedBody(attachedBody),
		mVisObject(visObject)
	{
	}

	void syncVis(const GeocentricToNedConverter& converter)
	{
		Vector3 pos = mAttachedBody->getPosition() + mAttachedBody->getOrientation() * mPropeller->getPositionRelBody();

		mVisObject->setPosition(converter.convertPosition(pos));
		mVisObject->setOrientation(osg::Quat(mPropeller->getRotationAngle(), osg::Vec3f(0, 0, 1)) * converter.convert(mAttachedBody->getOrientation() * mPropeller->getOrientationRelBody()));
	}

private:
	const PropellerComponent* mPropeller;
	const Positionable* mAttachedBody;
	vis::RootNodePtr mVisObject;
};

typedef std::shared_ptr<VisObjectsComponent> VisObjectsComponentPtr;

static osg::Vec3f readVec3f(const nlohmann::json& j)
{
	return osg::Vec3(j[0].get<double>(), j[1].get<double>(), j[2].get<double>());
}

static osg::Vec3f readOptionalVec3f(const nlohmann::json& j, const std::string& name, const osg::Vec3f& defaultValue)
{
	auto i = j.find(name);
	if (i != j.end())
	{
		return readVec3f(i.value());
	}
	return defaultValue;
}

static osg::Quat readQuat(const nlohmann::json& j)
{
	return osg::Quat(j.at("angleDeg").get<double>() * skybolt::math::degToRadD(), readVec3f(j.at("axis")));
}

static osg::Quat readOptionalQuat(const nlohmann::json& j, const std::string& name, const osg::Quat& defaultValue)
{
	auto i = j.find(name);
	if (i != j.end())
	{
		return readQuat(i.value());
	}
	return defaultValue;
}

static std::string getParentDirectory(const std::string& filename)
{
	boost::filesystem::path p(filename);
	return p.parent_path().string();
}

static void registerAssetSearchDirectory(const std::string& filename)
{
	osgDB::FilePathList& list = osgDB::Registry::instance()->getDataFilePathList();
	auto i = std::find(list.begin(), list.end(), filename);
	if (i != list.end())
	{
		list.push_back(filename);
	}
}

static void loadVisualModel(Entity* entity, const EntityFactory::Context& context, const VisObjectsComponentPtr& visObjectsComponent, const SimVisBindingsComponentPtr& simVisBindingComponent, const nlohmann::json& json)
{
	std::string filename = json.at("model").get<std::string>();
	vis::ModelConfig config;
	config.node = context.modelFactory->createModel(filename);

	registerAssetSearchDirectory(getParentDirectory(filename));

	vis::ModelPtr fuselageModel(new vis::Model(config));
	visObjectsComponent->addObject(fuselageModel);

	SimVisBindingPtr simVis(new SimpleSimVisBinding(entity, fuselageModel,
		readOptionalVec3f(json, "positionRelBody", osg::Vec3f()),
		readOptionalQuat(json, "orientationRelBody", osg::Quat())
	));
	simVisBindingComponent->bindings.push_back(simVis);
}

static void loadVisualMainRotor(Entity* entity, const EntityFactory::Context& context, const VisObjectsComponentPtr& visObjectsComponent, const SimVisBindingsComponentPtr& simVisBindingComponent, const nlohmann::json& json)
{
	std::string filename = json.at("model").get<std::string>();
	vis::ModelConfig config;
	config.node = context.modelFactory->createModel(filename);

	registerAssetSearchDirectory(getParentDirectory(filename));

	vis::ModelPtr mainRotorModel(new vis::Model(config));
	visObjectsComponent->addObject(mainRotorModel);

	auto rotor = entity->getFirstComponentRequired<MainRotorComponent>();
	auto node = entity->getFirstComponentRequired<Node>();

	SimVisBindingPtr simVis(new MainRotorVisComponent(rotor.get(), node.get(), mainRotorModel));
	simVisBindingComponent->bindings.push_back(simVis);
}

static void loadVisualTailRotor(Entity* entity, const EntityFactory::Context& context, const VisObjectsComponentPtr& visObjectsComponent, const SimVisBindingsComponentPtr& simVisBindingComponent, const nlohmann::json& json)
{
	std::string filename = json.at("model").get<std::string>();
	vis::ModelConfig config;
	config.node = context.modelFactory->createModel(filename);

	registerAssetSearchDirectory(getParentDirectory(filename));

	vis::ModelPtr tailRotorModel(new vis::Model(config));
	visObjectsComponent->addObject(tailRotorModel);

	auto rotor = entity->getFirstComponentRequired<PropellerComponent>();
	auto node = entity->getFirstComponentRequired<Node>();

	SimVisBindingPtr simVis(new PropellerVisComponent(rotor.get(), node.get(), tailRotorModel));
	simVisBindingComponent->bindings.push_back(simVis);
}

static void loadVisualCamera(Entity* entity, const EntityFactory::Context& context, const VisObjectsComponentPtr& visObjectsComponent, const SimVisBindingsComponentPtr& simVisBindingComponent, const nlohmann::json& json)
{
	vis::CameraPtr visCamera(new vis::Camera(1.0f));
	SimVisBindingPtr cameraSimVisBinding(new CameraSimVisBinding(entity, visCamera));
	simVisBindingComponent->bindings.push_back(cameraSimVisBinding);
}

struct PlanetStatsUpdater : vis::PlanetSurfaceListener, sim::Component
{
	PlanetStatsUpdater(EngineStats* stats, vis::PlanetSurface* surface)
		: mStats(stats), mSurface(surface)
	{
		mSurface->addListener(this);
	}

	~PlanetStatsUpdater()
	{
		mSurface->removeListener(this);
		mStats->tileLoadQueueSize -= mOwnTilesLoading;
	}

	void tileLoadRequested() override
	{
		++mStats->tileLoadQueueSize;
		++mOwnTilesLoading;
	}

	void tileLoaded() override
	{
		--mStats->tileLoadQueueSize;
		--mOwnTilesLoading;
	}

	void tileLoadCanceled() override
	{
		--mStats->tileLoadQueueSize;
		--mOwnTilesLoading;
	}

private:
	EngineStats* mStats;
	vis::PlanetSurface* mSurface;
	size_t mOwnTilesLoading = 0;
};

static osg::Texture2D* createCloudTexture(const std::string& filepath)
{
	osg::Image* image = vis::readImageWithCorrectOrientation(filepath);
	image->setInternalTextureFormat(vis::toSrgbInternalFormat(image->getInternalTextureFormat()));
	osg::Texture2D* texture = new osg::Texture2D(image);
	texture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
	return texture;
}

static void loadPlanet(Entity* entity, const EntityFactory::Context& context, const VisObjectsComponentPtr& visObjectsComponent, const SimVisBindingsComponentPtr& simVisBindingComponent, const nlohmann::json& json)
{
	double planetRadius = json.at("radius").get<double>();
	bool hasOcean = readOptionalOrDefault(json, "ocean", true);

	vis::PlanetConfig config;
	config.scheduler = context.scheduler;
	config.programs = context.programs;
	config.scene = context.scene;
	config.innerRadius = planetRadius;
	config.visFactoryRegistry = context.visFactoryRegistry.get();
	config.waterEnabled = hasOcean;
	
	{
		auto it = json.find("clouds");
		if (it != json.end())
		{
			const nlohmann::json& clouds = it.value();
			config.cloudsTexture = createCloudTexture(clouds.at("map"));
		}
	}

	{
		auto it = json.find("atmosphere");
		if (it != json.end())
		{
			const nlohmann::json& atmosphere = it.value();
			
			vis::BruentonAtmosphereConfig atmosphereConfig;
			atmosphereConfig.bottomRadius = planetRadius;
			atmosphereConfig.topRadius = planetRadius * 1.0094; // TODO: determine programatically from scale height

			if (auto coefficient = readOptional<double>(atmosphere, "earthReyleighScatteringCoefficient"))
			{
				atmosphereConfig.reyleighScatteringCoefficientCalculator = vis::createEarthReyleighScatteringCoefficientCalculator(*coefficient);
			}
			else if (auto table = readOptional<nlohmann::json>(atmosphere, "reyleighScatteringCoefficientTable"))
			{
				auto coefficients = table->at("coefficients");
				auto wavelengthsNm = table->at("wavelengthsNm");
				if (coefficients.size() != wavelengthsNm.size())
				{
					throw Exception("Must have equal number of coefficients and wavelengths");
				}

				atmosphereConfig.reyleighScatteringCoefficientCalculator = vis::createTableReyleighScatteringCoefficientCalculator(coefficients, wavelengthsNm);
			}
			else
			{
				throw Exception("Reyleigh scattering coefficient not defined");
			}

			atmosphereConfig.rayleighScaleHeight = atmosphere.at("rayleighScaleHeight").get<double>();
			atmosphereConfig.mieScaleHeight = atmosphere.at("mieScaleHeight").get<double>();
			atmosphereConfig.mieAngstromAlpha = atmosphere.at("mieAngstromAlpha").get<double>();
			atmosphereConfig.mieAngstromBeta = atmosphere.at("mieAngstromBeta").get<double>();
			atmosphereConfig.mieSingleScatteringAlbedo = atmosphere.at("mieSingleScatteringAlbedo").get<double>();
			atmosphereConfig.miePhaseFunctionG = atmosphere.at("miePhaseFunctionG").get<double>();
			atmosphereConfig.useEarthOzone = readOptionalOrDefault<bool>(atmosphere, "useEarthOzone", false);

			config.atmosphereConfig = atmosphereConfig;
		}
	}
	
	const nlohmann::json& layers = json.at("surface");
	{
		nlohmann::json elevation = layers.at("elevation");
		config.elevationMaxLodLevel = elevation.at("maxLevel");
		config.planetTileSources.elevation = context.tileSourceFactory->createTileSourceFromJson(elevation);
	}
	{
		nlohmann::json albedo = layers.at("albedo");
		config.albedoMaxLodLevel = albedo.at("maxLevel");
		config.planetTileSources.albedo = context.tileSourceFactory->createTileSourceFromJson(albedo);

	}
	auto it = layers.find("attribute");
	if (it != layers.end())
	{
		config.planetTileSources.attribute = context.tileSourceFactory->createTileSourceFromJson(*it);
	}

	{
		auto it = json.find("features");
		if (it != json.end())
		{
			const nlohmann::json& features = it.value();
			config.featuresDirectory = context.fileLocator(features.at("directory"), file::FileLocatorMode::Required);
		}
	}

	vis::PlanetPtr visObject(new vis::Planet(config));
	entity->addComponent(std::make_shared<Node>());

	entity->addComponent(simVisBindingComponent);

	SimVisBindingPtr simVis(new PlanetVisBinding(context.julianDateProvider, entity, visObject));
	simVisBindingComponent->bindings.push_back(simVis);

	if (visObject->getWaterStateSet())
	{
		auto binding = std::make_shared<WakeBinding>(context.simWorld, visObject->getWaterStateSet());
		simVisBindingComponent->bindings.push_back(binding);
	}

	entity->addComponent(visObjectsComponent);
	visObjectsComponent->addObject(visObject);

	auto altitudeProvider = std::make_shared<vis::TileAsyncPlanetAltitudeProvider>(context.scheduler, config.planetTileSources.elevation, config.elevationMaxLodLevel);
	auto planetComponent = std::make_shared<PlanetComponent>(planetRadius, hasOcean, altitudeProvider);
	entity->addComponent(planetComponent);

	entity->addComponent(ComponentPtr(new NameComponent("Earth", context.namedObjectRegistry, entity)));

	std::shared_ptr<PlanetStatsUpdater> statsUpdater = std::make_shared<PlanetStatsUpdater>(context.stats, static_cast<vis::Planet*>(visObject.get())->getSurface());
	entity->addComponent(statsUpdater);
}

typedef std::function<void(Entity*, const EntityFactory::Context&, VisObjectsComponentPtr&, const SimVisBindingsComponentPtr&, const nlohmann::json&)> VisComponentLoader;

EntityPtr EntityFactory::createEntityFromJson(const nlohmann::json& json, const std::string& templateName, const std::string& instanceName, const Vector3& position, const Quaternion& orientation) const
{
	EntityPtr entity = std::make_shared<sim::Entity>();

	entity->addComponent(ComponentPtr(new NameComponent(instanceName, mContext.namedObjectRegistry, entity.get())));
	entity->addComponent(ComponentPtr(new TemplateNameComponent(templateName)));

	SimVisBindingsComponentPtr simVisBindingComponent(new SimVisBindingsComponent);
	entity->addComponent(simVisBindingComponent);

	VisObjectsComponentPtr visObjectsComponent(new VisObjectsComponent(mContext.scene));
	entity->addComponent(visObjectsComponent);

	ComponentFactoryContext componentFactoryContext;
	componentFactoryContext.julianDateProvider = mContext.julianDateProvider;
	componentFactoryContext.scheduler = mContext.scheduler;
	componentFactoryContext.simWorld = mContext.simWorld;
	componentFactoryContext.stats = mContext.stats;

	const nlohmann::json& components = json.at("components");
	for (const auto& component : components)
	{
		for (nlohmann::json::const_iterator componentIt = component.begin(); componentIt != component.end(); ++componentIt)
		{
			std::string key = componentIt.key();
			const nlohmann::json& content = componentIt.value();
			
			// Sim components
			{
				auto it = mContext.componentFactoryRegistry->find(key);
				if (it != mContext.componentFactoryRegistry->end())
				{
					ComponentFactoryPtr factory = it->second;
					auto component = factory->create(entity.get(), componentFactoryContext, content);
					if (component)
					{
						entity->addComponent(component);
					}
				}
			}
			// Vis components
			{
				static std::map<std::string, VisComponentLoader> visComponentLoaders =
				{
					{"camera", loadVisualCamera },
					{ "visualModel", loadVisualModel },
					{ "visualMainRotor", loadVisualMainRotor },
					{ "visualTailRotor", loadVisualTailRotor },
					{ "planet", loadPlanet }
				};

				auto it = visComponentLoaders.find(key);
				if (it != visComponentLoaders.end())
				{
					it->second(entity.get(), mContext, visObjectsComponent, simVisBindingComponent, content);
				}
			}
		}
	}

	Node* node = entity->getFirstComponent<Node>().get();
	if (node)
	{
		node->setPosition(position);
		node->setOrientation(orientation);
	}

	return entity;
}

EntityFactory::EntityFactory(const EntityFactory::Context& context, const std::vector<boost::filesystem::path>& entityFilenames) :
	mContext(context)
{
	assert(context.julianDateProvider);
	assert(context.namedObjectRegistry);
	assert(context.programs);
	assert(context.simWorld);
	assert(context.stats);
	assert(context.tileSourceFactory);
	assert(context.scene);
	mBuiltinTemplates = {
		{"SunBillboard", [this] {return createSun(); }},
		{"MoonBillboard", [this] {return createMoon(); }},
		{"Stars", [this] {return createStars(); }},
		{"Polyline", [this] {return createPolyline(); }}
	};

	for (const boost::filesystem::path& filename : entityFilenames)
	{
		std::string name = filename.stem().string();
		mTemplateJsonMap[name] = readJsonFile(filename.string());
		mTemplateNames.push_back(name);
	}
}

EntityPtr EntityFactory::createEntity(const std::string& templateName, const std::string& nameIn, const Vector3& position, const Quaternion& orientation) const
{
	{
		auto i = mTemplateJsonMap.find(templateName);
		if (i != mTemplateJsonMap.end())
		{
			std::string name = nameIn.empty() ? createUniqueObjectName(templateName) : nameIn;
			try
			{
				return createEntityFromJson(i->second, templateName, name, position, orientation);
			}
			catch (const std::exception& e)
			{
				throw Exception("Error loading '" + templateName + "': " + e.what());
			}
		}
	}

	// Try builtin types
	{
		auto i = mBuiltinTemplates.find(templateName);
		if (i != mBuiltinTemplates.end())
		{
			return i->second();
		}
	}

	throw std::runtime_error("Invalid templateName: " + templateName);
}

const float sunDistance = 10000;
const float moonDistance = sunDistance;
const float sunDiameter = 2.0f * tan(skybolt::math::degToRadF() * 0.53f * 0.5f) * sunDistance;
const float moonDiameter = 2.0f * tan(skybolt::math::degToRadF() * 0.52f * 0.5f) * moonDistance;

EntityPtr EntityFactory::createSun() const
{
	osg::StateSet* ss = new osg::StateSet;
	ss->setAttribute(mContext.programs->sun);
	ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);

	osg::Depth* depth = new osg::Depth;
	depth->setWriteMask(false);
	ss->setAttributeAndModes(depth, osg::StateAttribute::ON);

	osg::Texture2D* texture = new osg::Texture2D(osgDB::readImageFile("Environment/Space/SunDisc.png"));
	texture->setInternalFormat(vis::toSrgbInternalFormat(texture->getInternalFormat()));
	ss->setTextureAttributeAndModes(0, texture);
	ss->addUniform(vis::createUniformSampler2d("albedoSampler", 0));

	osg::ref_ptr<osg::BlendFunc> blendFunc = new osg::BlendFunc;
	ss->setAttributeAndModes(blendFunc);

	EntityPtr object(new Entity());
	object->addComponent(std::make_shared<Node>());

	float diameterScale = 1.15f; // account for disk in texture being slightly smaller than texture size
	vis::RootNodePtr node(new vis::CameraRelativeBillboard(ss, sunDiameter * diameterScale, sunDiameter * diameterScale, sunDistance));

	SimVisBindingsComponentPtr simVisBindingComponent(new SimVisBindingsComponent);
	object->addComponent(simVisBindingComponent);

	SimVisBindingPtr simVis(new CelestialObjectVisBinding(mContext.julianDateProvider, calcSunEclipticPosition, node));
	simVisBindingComponent->bindings.push_back(simVis);

	VisObjectsComponentPtr visObjectsComponent(new VisObjectsComponent(mContext.scene));
	visObjectsComponent->addObject(node);
	object->addComponent(visObjectsComponent);

	vis::LightPtr light(new vis::Light(osg::Vec3f(-1,0,0)));
	visObjectsComponent->addObject(light);

	{ // TODO: reused sun ecliptic position calculated for the billboard above to avoid recalculating
		SimVisBindingPtr simVis(new CelestialObjectVisBinding(mContext.julianDateProvider, calcSunEclipticPosition, light));
		simVisBindingComponent->bindings.push_back(simVis);
	}

	return object;
}

EntityPtr EntityFactory::createMoon() const
{
	osg::StateSet* ss = new osg::StateSet;
	ss->setAttribute(mContext.programs->moon);
	ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);

	osg::Depth* depth = new osg::Depth;
	depth->setWriteMask(false);
	ss->setAttributeAndModes(depth, osg::StateAttribute::ON);

	osg::Uniform* moonPhaseUniform = new osg::Uniform("moonPhase", 0.5f);
	ss->addUniform(moonPhaseUniform);

	osg::Texture2D* texture = new osg::Texture2D(vis::readImageWithCorrectOrientation("Environment/Space/MoonDisc.jpg"));
	texture->setInternalFormat(vis::toSrgbInternalFormat(texture->getInternalFormat()));
	ss->setTextureAttributeAndModes(0, texture);
	ss->addUniform(vis::createUniformSampler2d("albedoSampler", 0));

	EntityPtr object(new Entity());
	object->addComponent(std::make_shared<Node>());

	vis::RootNodePtr node(new vis::CameraRelativeBillboard(ss, moonDiameter, moonDiameter, moonDistance));

	SimVisBindingsComponentPtr simVisBindingComponent(new SimVisBindingsComponent);
	SimVisBindingPtr simVis(new MoonVisBinding(mContext.julianDateProvider, moonPhaseUniform, node));
	simVisBindingComponent->bindings.push_back(simVis);
	object->addComponent(simVisBindingComponent);

	VisObjectsComponentPtr visObjectsComponent(new VisObjectsComponent(mContext.scene));
	visObjectsComponent->addObject(node);
	object->addComponent(visObjectsComponent);

	return object;
}

EntityPtr EntityFactory::createStars() const
{
	vis::StarfieldConfig config;
	config.program = mContext.programs->starfield;
	vis::RootNodePtr starfield(new vis::Starfield(config));

	auto calcStarfieldEclipticPosition = [](double julianDate) { return LatLon(0, 0); };

	EntityPtr object(new Entity());
	object->addComponent(std::make_shared<Node>());

	SimVisBindingsComponentPtr simVisBindingComponent(new SimVisBindingsComponent);
	SimVisBindingPtr simVis(new CelestialObjectVisBinding(mContext.julianDateProvider, calcStarfieldEclipticPosition, starfield));
	simVisBindingComponent->bindings.push_back(simVis);
	object->addComponent(simVisBindingComponent);

	VisObjectsComponentPtr visObjectsComponent(new VisObjectsComponent(mContext.scene));
	visObjectsComponent->addObject(starfield);
	object->addComponent(visObjectsComponent);

	return object;
}

EntityPtr EntityFactory::createPolyline() const
{
	vis::Polyline::Params params;
	params.program = mContext.programs->unlitColored;

	vis::PolylinePtr polyline(new vis::Polyline(params));

	EntityPtr object(new Entity());
	object->addComponent(std::make_shared<Node>());

	SimVisBindingsComponentPtr simVisBindingComponent(new SimVisBindingsComponent);
	simVisBindingComponent->bindings.push_back(std::make_shared<PolylineVisBinding>(polyline));
	object->addComponent(simVisBindingComponent);

	VisObjectsComponentPtr visObjectsComponent(new VisObjectsComponent(mContext.scene));
	visObjectsComponent->addObject(polyline);

	object->addComponent(visObjectsComponent);

	return object;
}

std::string EntityFactory::createUniqueObjectName(const std::string& baseName) const
{
	for (int i = 1; i < INT_MAX; ++i)
	{
		std::string name = baseName + std::to_string(i);
		if (mContext.namedObjectRegistry->getObjectByName(name) == nullptr)
		{
			return name;
		}
	}
	throw skybolt::Exception("Could not create unique object name from base name: " + baseName);
}

