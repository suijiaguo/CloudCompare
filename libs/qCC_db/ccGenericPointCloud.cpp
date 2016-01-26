//##########################################################################
//#                                                                        #
//#                            CLOUDCOMPARE                                #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 of the License.               #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

#include "ccGenericPointCloud.h"

//CCLib
#include <Neighbourhood.h>
#include <DistanceComputationTools.h>

//Local
#include "ccOctree.h"
#include "ccSensor.h"
#include "ccGenericGLDisplay.h"

ccGenericPointCloud::ccGenericPointCloud(QString name)
	: ccShiftedObject(name)
	, m_pointsVisibility(0)
	, m_pointSize(0)
{
	setVisible(true);
	lockVisibility(false);
}

ccGenericPointCloud::ccGenericPointCloud(const ccGenericPointCloud& cloud)
	: ccShiftedObject(cloud)
	, m_pointsVisibility(cloud.m_pointsVisibility)
	, m_pointSize(cloud.m_pointSize)
{
}

ccGenericPointCloud::~ccGenericPointCloud()
{
	clear();
}

void ccGenericPointCloud::clear()
{
	unallocateVisibilityArray();
	deleteOctree();
	enableTempColor(false);
}

bool ccGenericPointCloud::resetVisibilityArray()
{
	if (!m_pointsVisibility)
	{
		m_pointsVisibility = new VisibilityTableType();
		m_pointsVisibility->link();
	}

	if (!m_pointsVisibility->resize(size()))
	{
		unallocateVisibilityArray();
		return false;
	}

	m_pointsVisibility->fill(POINT_VISIBLE); //by default, all points are visible

	return true;
}

void ccGenericPointCloud::unallocateVisibilityArray()
{
	if (m_pointsVisibility)
	{
		m_pointsVisibility->release();
		m_pointsVisibility = 0;
	}
}

bool ccGenericPointCloud::isVisibilityTableInstantiated() const
{
	return m_pointsVisibility && m_pointsVisibility->isAllocated();
}

unsigned char ccGenericPointCloud::testVisibility(const CCVector3& P) const
{
	unsigned char bestVisibility = 255; //impossible value

	for (ccHObject::Container::const_iterator it = m_children.begin(); it != m_children.end(); ++it)
	{
		if ((*it)->isKindOf(CC_TYPES::SENSOR))
		{
			unsigned char visibility = static_cast<ccSensor*>(*it)->checkVisibility(P);

			if (visibility == POINT_VISIBLE)
			{
				return POINT_VISIBLE; //shortcut
			}

			bestVisibility = std::min<unsigned char>(visibility,bestVisibility);
		}
	}

	return (bestVisibility == 255 ? POINT_VISIBLE : bestVisibility);
}

void ccGenericPointCloud::deleteOctree()
{
	ccOctree* oct = getOctree();
	if (oct)
		removeChild(oct);
}

ccOctree* ccGenericPointCloud::getOctree()
{
	for (size_t i=0; i<m_children.size(); ++i)
	{
		if (m_children[i]->isA(CC_TYPES::POINT_OCTREE))
			return static_cast<ccOctree*>(m_children[i]);
	}

	return NULL;
}

ccOctree* ccGenericPointCloud::computeOctree(CCLib::GenericProgressCallback* progressCb, bool autoAddChild/*=true*/)
{
	deleteOctree();
	ccOctree* octree = new ccOctree(this);
	if (octree->build(progressCb) > 0)
	{
		octree->setDisplay(getDisplay());
		octree->setVisible(true);
		octree->setEnabled(false);
		if (autoAddChild)
		{
			addChild(octree);
		}
	}
	else
	{
		delete octree;
		octree = NULL;
	}

	return octree;
}

CCLib::ReferenceCloud* ccGenericPointCloud::getTheVisiblePoints() const
{
	unsigned count = size();
	assert(count == m_pointsVisibility->currentSize());

	if (!m_pointsVisibility || m_pointsVisibility->currentSize() != count)
	{
		ccLog::Warning("[ccGenericPointCloud::getTheVisiblePoints] No visibility table instantiated!");
		return 0;
	}

	//count the number of points to copy
	unsigned pointCount = 0;
	{
		for (unsigned i=0; i<count; ++i)
			if (m_pointsVisibility->getValue(i) == POINT_VISIBLE)
				++pointCount;
	}

	if (pointCount == 0)
	{
		ccLog::Warning("[ccGenericPointCloud::getTheVisiblePoints] No point in selection");
		return 0;
	}

	//we create an entity with the 'visible' vertices only
	CCLib::ReferenceCloud* rc = new CCLib::ReferenceCloud(const_cast<ccGenericPointCloud*>(this));
	if (rc->reserve(pointCount))
	{
		for (unsigned i=0; i<count; ++i)
			if (m_pointsVisibility->getValue(i) == POINT_VISIBLE)
				rc->addPointIndex(i); //can't fail (see above)
	}
	else
	{
		delete rc;
		rc = 0;
		ccLog::Error("[ccGenericPointCloud::getTheVisiblePoints] Not enough memory!");
	}

	return rc;
}

ccBBox ccGenericPointCloud::getOwnBB(bool withGLFeatures/*=false*/)
{
	ccBBox box;

	if (size())
	{
		getBoundingBox(box.minCorner(), box.maxCorner());
		box.setValidity(true);
	}
	
	return box;
}

bool ccGenericPointCloud::toFile_MeOnly(QFile& out) const
{
	if (!ccHObject::toFile_MeOnly(out))
		return false;

	//'global shift & scale' (dataVersion>=39)
	saveShiftInfoToFile(out);
	
	//'visibility' array (dataVersion>=20)
	bool hasVisibilityArray = isVisibilityTableInstantiated();
	if (out.write((const char*)&hasVisibilityArray,sizeof(bool)) < 0)
		return WriteError();
	if (hasVisibilityArray)
	{
		assert(m_pointsVisibility);
		if (!ccSerializationHelper::GenericArrayToFile(*m_pointsVisibility,out))
			return false;
	}

	//'point size' (dataVersion>=24)
	if (out.write((const char*)&m_pointSize,1) < 0)
		return WriteError();

	return true;
}

bool ccGenericPointCloud::fromFile_MeOnly(QFile& in, short dataVersion, int flags)
{
	if (!ccHObject::fromFile_MeOnly(in, dataVersion, flags))
		return false;

	if (dataVersion < 20)
		return CorruptError();

	if (dataVersion < 33)
	{
		//'coordinates shift' (dataVersion>=20)
		if (in.read((char*)m_globalShift.u,sizeof(double)*3) < 0)
			return ReadError();

		m_globalScale = 1.0;
	}
	else
	{
		//'global shift & scale' (dataVersion>=33)
		if (!loadShiftInfoFromFile(in))
			return ReadError();
	}

	//'visibility' array (dataVersion>=20)
	bool hasVisibilityArray = false;
	if (in.read((char*)&hasVisibilityArray,sizeof(bool)) < 0)
		return ReadError();
	if (hasVisibilityArray)
	{
		if (!m_pointsVisibility)
		{
			m_pointsVisibility = new VisibilityTableType();
			m_pointsVisibility->link();
		}
		if (!ccSerializationHelper::GenericArrayFromFile(*m_pointsVisibility,in,dataVersion))
		{
			unallocateVisibilityArray();
			return false;
		}
	}

	//'point size' (dataVersion>=24)
	if (dataVersion >= 24)
	{
		if (in.read((char*)&m_pointSize,1) < 0)
			return WriteError();
	}
	else
	{
		m_pointSize = 0; //= follows default setting
	}

	return true;
}

void ccGenericPointCloud::importParametersFrom(const ccGenericPointCloud* cloud)
{
	if (!cloud)
	{
		assert(false);
		return;
	}

	//original center
	setGlobalShift(cloud->getGlobalShift());
	setGlobalScale(cloud->getGlobalScale());
	//keep the transformation history!
	setGLTransformationHistory(cloud->getGLTransformationHistory());
	//custom point size
	setPointSize(cloud->getPointSize());
	//meta-data
	setMetaData(cloud->metaData());
}

#include "ccPointCloud.h"
#include <ScalarField.h>

bool ccGenericPointCloud::isClicked(const CCVector2d& clickPos,
									int& nearestPointIndex,
									double& nearestSquareDist,
									const double* MM,
									const double* MP,
									const int* VP,
									double pickWidth/*=2.0*/,
									double pickHeight/*=2.0*/)
{
	ccGLMatrix trans;
	bool noGLTrans = !getAbsoluteGLTransformation(trans);

	//back project the clicked point in 3D
	CCVector3d clickPosd(clickPos.x, clickPos.y, 0);
	CCVector3d X(0,0,0);
	ccGL::Unproject<double, double>(clickPosd, MM, MP, VP, X);

	nearestPointIndex = -1;
	nearestSquareDist = -1.0;
	
	ccOctree* octree = getOctree();
	if (octree && pickWidth == pickHeight && getDisplay())
	{
		//we can now use the octree to do faster point picking
		CCVector3d clickPosd2(clickPos.x, clickPos.y, 1);
		CCVector3d Y(0,0,0);
		ccGL::Unproject<double, double>(clickPosd2, MM, MP, VP, Y);

		CCVector3d dir = Y-X;
		dir.normalize();
		CCVector3 udir = CCVector3::fromArray(dir.u);
		CCVector3 origin = CCVector3::fromArray(X.u);

		if (!noGLTrans)
		{
			trans.invert();
			trans.apply(origin);
			trans.applyRotation(udir);
		}

		double fovOrRadius = 0;
		const ccViewportParameters& viewParams = getDisplay()->getViewportParameters();
		bool isFOV = viewParams.perspectiveView;
		if (isFOV)
		{
			fovOrRadius = 0.002 * pickWidth; //empirical conversion from pixels to FOV angle (in radians)
		}
		else
		{
			fovOrRadius = pickWidth * viewParams.pixelSize / 2;
		}

#ifdef _DEBUG
		CCLib::ScalarField* sf = 0;
		if (getClassID() == CC_TYPES::POINT_CLOUD)
		{
			ccPointCloud* pc = static_cast<ccPointCloud*>(this);
			int sfIdx = pc->getScalarFieldIndexByName("octree_picking");
			if (sfIdx < 0)
			{
				sfIdx = pc->addScalarField("octree_picking");
			}
			if (sfIdx >= 0)
			{
				pc->setCurrentScalarField(sfIdx);
				pc->setCurrentDisplayedScalarField(sfIdx);
				pc->showSF(true);
				sf = pc->getScalarField(sfIdx);
			}
		}
#endif

		std::vector<CCLib::DgmOctree::PointDescriptor> points;
		if (octree->rayCast(udir, origin, fovOrRadius, isFOV, CCLib::DgmOctree::RC_NEAREST_POINT, points))
		{
#ifdef _DEBUG
			if (sf)
			{
				sf->computeMinAndMax();
				getDisplay()->redraw();
			}
#endif
			if (!points.empty())
			{
				nearestPointIndex = points.back().pointIndex;
				nearestSquareDist = points.back().squareDistd;
				return true;
			}
			return false;
		}
		else
		{
			ccLog::Warning("[Point picking] Failed to use the octree. We'll fall back to the slow process...");
		}
	}

#if defined(_OPENMP)
#pragma omp parallel for
#endif
	//brute force works quite well in fact?!
	for (unsigned i=0; i<size(); ++i)
	{
		const CCVector3* P = getPoint(i);
		CCVector3d Qs;
		if (noGLTrans)
		{
			ccGL::Project<PointCoordinateType, double>(*P,MM,MP,VP,Qs);
		}
		else
		{
			CCVector3 Q = *P;
			trans.apply(Q);
			ccGL::Project<PointCoordinateType, double>(Q,MM,MP,VP,Qs);
		}

		if (fabs(Qs.x-clickPos.x) <= pickWidth && fabs(Qs.y-clickPos.y) <= pickHeight)
		{
			double squareDist = CCVector3d(X.x-P->x, X.y-P->y, X.z-P->z).norm2d();
			if (nearestPointIndex < 0 || squareDist < nearestSquareDist)
			{
				nearestSquareDist = squareDist;
				nearestPointIndex = static_cast<int>(i);
			}
		}
	}

	return (nearestPointIndex >= 0);
}
