/* Copyright 2012-2020 Matthew Reid
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#version 330 core

in vec4 osg_Vertex;
in vec4 osg_MultiTexCoord0;

out vec3 texCoord;

void main()
{
	gl_Position = osg_Vertex * vec4(2) - vec4(1);
	texCoord = osg_MultiTexCoord0.xyz;
}