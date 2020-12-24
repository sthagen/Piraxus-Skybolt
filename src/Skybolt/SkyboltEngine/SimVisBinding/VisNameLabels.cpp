/* Copyright 2012-2020 Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "VisNameLabels.h"
#include "GeocentricToNedConverter.h"
#include "TemplateNameComponent.h"
#include <SkyboltSim/Components/NameComponent.h>
#include "SkyboltVis/ShaderProgramRegistry.h"
#include <osg/Depth>
#include <osg/Geode>
#include <osgText/Text>

namespace skybolt {

using namespace sim;

VisNameLabels::VisNameLabels(World* world, osg::Group* parent, const vis::ShaderPrograms& programs) :
	SimVisObjectsReflector<osg::MatrixTransform*>(world, parent)
{
	osg::ref_ptr<osg::StateSet> ss = mGroup->getOrCreateStateSet();
	{
		ss->setAttributeAndModes(programs.hudText, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);

		ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
		ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
		ss->setMode(GL_BLEND, osg::StateAttribute::ON);
		ss->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

		osg::Depth* depth = new osg::Depth;
		depth->setWriteMask(false);
		depth->setFunction(osg::Depth::ALWAYS);
		ss->setAttributeAndModes(depth, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE | osg::StateAttribute::PROTECTED);
	}
}

VisNameLabels::~VisNameLabels()
{
}

void VisNameLabels::syncVis(const GeocentricToNedConverter& converter)
{
	for (const auto& entry : getObjectsMap())
	{
		Entity* entity = entry.first;
		osg::MatrixTransform* transform = entry.second;

		if (applyVisibility(*entity, transform))
		{
			osg::Matrix matrix = transform->getMatrix();
			matrix.setTrans(converter.convertPosition(*getPosition(*entity)));
			transform->setMatrix(matrix);
		}
	}
}

boost::optional<osg::MatrixTransform*> VisNameLabels::createObject(const sim::EntityPtr& entity)
{
	boost::optional<sim::Vector3> position = getPosition(*entity);
	if (position && entity->getFirstComponent<TemplateNameComponent>())
	{
		std::string name = getName(*entity);
		if (!name.empty())
		{
			osgText::Text* text = new osgText::Text();
			text->setFont("fonts/verdana.ttf");
			text->setText(name);
			text->setCharacterSize(0.1); // scaled in shader to be approximately font point size / 100
			text->setUseDisplayList(false);
			text->setUseVertexBufferObjects(true);
			text->setUseVertexArrayObject(true);
			text->setCullingActive(false);

			osg::Geode* geode = new osg::Geode();
			geode->addDrawable(text);

			osg::MatrixTransform* transform = new osg::MatrixTransform();
			transform->addChild(geode);
			return transform;
		}
	}
	return boost::none;
}

} // namespace skybolt