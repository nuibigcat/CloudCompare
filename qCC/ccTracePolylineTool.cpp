//##########################################################################
//#                                                                        #
//#                              CLOUDCOMPARE                              #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 or later of the License.      #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

#include "ccTracePolylineTool.h"

//Local
#include "mainwindow.h"

//common
#include <ccPickingHub.h>

//CCLib
#include <ManualSegmentationTools.h>
#include <SquareMatrix.h>

//qCC_db
#include <ccLog.h>
#include <ccHObject.h>
#include <ccPolyline.h>
#include <ccGenericPointCloud.h>
#include <ccPointCloud.h>
#include <ccMesh.h>
#include <ccHObjectCaster.h>
#include <cc2DViewportObject.h>

//qCC_gl
#include <ccGLWindow.h>

//Qt
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressDialog>

//System
#include <assert.h>

#include <iostream>

#ifdef USE_STOCKER
#include "stocker_parser.h"
#endif // USE_STOCKER


ccTracePolylineTool::SegmentGLParams::SegmentGLParams(ccGenericGLDisplay* display, int x , int y)
{
	if (display)
	{
		display->getGLCameraParameters(params);
		QPointF pos2D = display->toCornerGLCoordinates(x, y);
		clickPos = CCVector2d(pos2D.x(), pos2D.y());
	}
}

ccTracePolylineTool::ccTracePolylineTool(ccPickingHub* pickingHub, QWidget* parent)
	: ccOverlayDialog(parent)
	, Ui::TracePolyLineDlg()
	, m_polyTip(0)
	, m_polyTipVertices(0)
	, m_poly3D(0)
	, m_poly3DVertices(0)
	, m_done(false)
	, m_pickingHub(pickingHub)
	, m_trace_mode(0)
	, m_dest_prim_group(nullptr)
{
	assert(pickingHub);

	setupUi(this);
	setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);

	connect(saveToolButton, SIGNAL(clicked()), this, SLOT(exportLine()));
	connect(resetToolButton, SIGNAL(clicked()), this, SLOT(resetLine()));
	connect(continueToolButton, SIGNAL(clicked()), this, SLOT(continueEdition()));
	connect(validButton, SIGNAL(clicked()), this, SLOT(apply()));
	connect(cancelButton, SIGNAL(clicked()), this, SLOT(cancel()));
	connect(widthSpinBox, SIGNAL(valueChanged(int)), this, SLOT(onWidthSizeChanged(int)));

	//add shortcuts
	addOverridenShortcut(Qt::Key_Escape); //escape key for the "cancel" button
	addOverridenShortcut(Qt::Key_Return); //return key for the "apply" button
	connect(this, SIGNAL(shortcutTriggered(int)), this, SLOT(onShortcutTriggered(int)));

	m_polyTipVertices = new ccPointCloud("Tip vertices");
	m_polyTipVertices->reserve(2);
	m_polyTipVertices->addPoint(CCVector3(0, 0, 0));
	m_polyTipVertices->addPoint(CCVector3(1, 1, 1));
	m_polyTipVertices->setEnabled(false);

	m_polyTip = new ccPolyline(m_polyTipVertices);
	m_polyTip->setForeground(true);
	m_polyTip->setTempColor(ccColor::green);
	m_polyTip->set2DMode(true);
	m_polyTip->reserve(2);
	m_polyTip->addPointIndex(0, 2);
	m_polyTip->setWidth(widthSpinBox->value() < 2 ? 0 : widthSpinBox->value()); //'1' is equivalent to the default line size
	m_polyTip->addChild(m_polyTipVertices);

	validButton->setEnabled(false);
	label_4->setVisible(false);
	DistanceDoubleSpinBox->setVisible(false);
}

ccTracePolylineTool::~ccTracePolylineTool()
{
	if (m_polyTip)
		delete m_polyTip;
	//DGM: already a child of m_polyTip
	//if (m_polyTipVertices)
	//	delete m_polyTipVertices;

	if (m_poly3D)
		delete m_poly3D;
	//DGM: already a child of m_poly3D
	//if (m_poly3DVertices)
	//	delete m_poly3DVertices;
}

void ccTracePolylineTool::onShortcutTriggered(int key)
{
	switch (key)
	{
	case Qt::Key_Return:
		apply();
		return;

	case Qt::Key_Escape:
		cancel();
		return;

	case Qt::Key_Space:
		continueToolButton->click();
		return;

	default:
		//nothing to do
		break;
	}
}

ccPolyline* ccTracePolylineTool::polylineOverSampling(unsigned steps) const
{
	if (!m_poly3D || !m_poly3DVertices || m_segmentParams.size() != m_poly3DVertices->size())
	{
		assert(false);
		return 0;
	}

	if (steps <= 1)
	{
		//nothing to do
		return 0;
	}

	ccHObject::Container clouds, meshes;
	for (ccHObject* v : m_associatedWin->getSceneDB()) {
		v->filterChildren(clouds, true, CC_TYPES::POINT_CLOUD, false, m_associatedWin);
		v->filterChildren(meshes, true, CC_TYPES::MESH, false, m_associatedWin);
	}

	if (clouds.empty() && meshes.empty())
	{
		//no entity is currently displayed?!
		assert(false);
		return 0;
	}

	unsigned n_verts = m_poly3DVertices->size();
	unsigned n_segments = m_poly3D->size() - (m_poly3D->isClosed() ? 0 : 1);
	unsigned end_size = n_segments * steps + (m_poly3D->isClosed() ? 0 : 1);

	ccPointCloud* newVertices = new ccPointCloud();
	ccPolyline* newPoly = new ccPolyline(newVertices);
	newPoly->addChild(newVertices);

	if (	!newVertices->reserve(end_size)
		||	!newPoly->reserve(end_size) )
	{
		ccLog::Warning("[ccTracePolylineTool::PolylineOverSampling] Not enough memory");
		delete newPoly;
		return 0;
	}
	newVertices->importParametersFrom(m_poly3DVertices);
	newVertices->setName(m_poly3DVertices->getName());
	newVertices->setEnabled(m_poly3DVertices->isEnabled());
	newPoly->importParametersFrom(*m_poly3D);
	newPoly->setDisplay_recursive(m_poly3D->getDisplay());

	QProgressDialog pDlg(QString("Oversampling"), "Cancel", 0, static_cast<int>(end_size), m_associatedWin ? m_associatedWin->asWidget() : 0);
	pDlg.show();
	QCoreApplication::processEvents();

	for (unsigned i = 0; i < n_segments; ++i)
	{
		const CCVector3* p1 = m_poly3DVertices->getPoint(i);
		newVertices->addPoint(*p1);


		unsigned i2 = (i + 1) % n_verts;
		CCVector2d clickPos1/* = m_segmentParams[i].clickPos*/;
		{
			//we actually retro-project the 3D point in the second vertex camera frame so as to get a proper behavior
			CCVector3d P2D;
			m_segmentParams[i2].params.project(*p1, P2D, false);
			clickPos1 = CCVector2d(P2D.x, P2D.y);
		}

		CCVector2d v = m_segmentParams[i2].clickPos - clickPos1;
		v /= steps;

		for (unsigned j = 1; j < steps; j++)
		{
			CCVector2d vj = clickPos1 + v * j;

			CCVector3 nearestPoint;
			double nearestElementSquareDist = -1.0;

			//for each cloud
			for (size_t c = 0; c < clouds.size(); ++c)
			{
				ccGenericPointCloud* cloud = static_cast<ccGenericPointCloud*>(clouds[c]);
				if (!cloud->isDisplayedIn(m_associatedWin))
				{
					continue;
				}
				
				int nearestPointIndex = -1;
				double nearestSquareDist = 0;
				if (cloud->pointPicking(vj,
										m_segmentParams[i2].params,
										nearestPointIndex,
										nearestSquareDist,
										snapSizeSpinBox->value(),
										snapSizeSpinBox->value(),
										true))
				{
					if (nearestElementSquareDist < 0 || nearestSquareDist < nearestElementSquareDist)
					{
						nearestElementSquareDist = nearestSquareDist;
						nearestPoint = *cloud->getPoint(nearestPointIndex);
					}
				}
			}

			//for each mesh
			for (size_t m = 0; m < meshes.size(); ++m)
			{
				ccGenericMesh* mesh = static_cast<ccGenericMesh*>(meshes[m]);
				if (!mesh->isDisplayedIn(m_associatedWin))
				{
					continue;
				}

				int nearestTriIndex = -1;
				double nearestSquareDist = 0;
				CCVector3d _nearestPoint;

				if (mesh->trianglePicking(	vj,
											m_segmentParams[i2].params,
											nearestTriIndex,
											nearestSquareDist,
											_nearestPoint))
				{
					if (nearestElementSquareDist < 0 || nearestSquareDist < nearestElementSquareDist)
					{
						nearestElementSquareDist = nearestSquareDist;
						nearestPoint = CCVector3::fromArray(_nearestPoint.u);
					}
				}
			}

			if (nearestElementSquareDist >= 0)
			{
				newVertices->addPoint(nearestPoint);
			}

			if (pDlg.wasCanceled())
			{
				steps = 0; //quick finish ;)
				break;
			}
			pDlg.setValue(pDlg.value() + 1);
		}
	}

	//add last point
	if (!m_poly3D->isClosed())
	{
		newVertices->addPoint(*m_poly3DVertices->getPoint(n_verts - 1));
	}

	newVertices->shrinkToFit();
	newPoly->addPointIndex(0, newVertices->size());

	return newPoly;
}

bool ccTracePolylineTool::linkWith(ccGLWindow* win)
{
	assert(m_polyTip);
	assert(!m_poly3D || !win);

	ccGLWindow* oldWin = m_associatedWin;

	if (!ccOverlayDialog::linkWith(win))
	{
		return false;
	}

	if (oldWin)
	{
		oldWin->removeFromOwnDB(m_polyTip);
		oldWin->disconnect(this);

		if (m_polyTip)
			m_polyTip->setDisplay(0);
	}

	if (m_associatedWin)
	{
		connect(m_associatedWin, SIGNAL(rightButtonClicked(int, int)), this, SLOT(closePolyLine(int, int)));
		connect(m_associatedWin, SIGNAL(mouseMoved(int, int, Qt::MouseButtons)), this, SLOT(updatePolyLineTip(int, int, Qt::MouseButtons)));
	}

	return true;
}

static int s_defaultPickingRadius = 1;
static int s_overSamplingCount = 1;
bool ccTracePolylineTool::start()
{
	assert(m_polyTip);
	assert(!m_poly3D);

	if (!m_associatedWin)
	{
		ccLog::Warning("[Trace Polyline Tool] No associated window!");
		return false;
	}

	m_associatedWin->setUnclosable(true);
	m_associatedWin->addToOwnDB(m_polyTip);
	if (m_pickingHub)
	{
		m_pickingHub->removeListener(this);
	}
	m_associatedWin->setPickingMode(ccGLWindow::NO_PICKING);
	m_associatedWin->setInteractionMode(	ccGLWindow::TRANSFORM_CAMERA()
										|	ccGLWindow::INTERACT_SIG_RB_CLICKED
										|	ccGLWindow::INTERACT_SHIFT_PAN
										|	ccGLWindow::INTERACT_SIG_MOUSE_MOVED);
	m_associatedWin->setCursor(Qt::CrossCursor);

	snapSizeSpinBox->blockSignals(true);
	snapSizeSpinBox->setValue(s_defaultPickingRadius);
	snapSizeSpinBox->blockSignals(false);

	oversampleSpinBox->blockSignals(true);
	oversampleSpinBox->setValue(s_overSamplingCount);
	oversampleSpinBox->blockSignals(false);

	resetLine(); //to reset the GUI

	return ccOverlayDialog::start();
}

void ccTracePolylineTool::stop(bool accepted)
{
	assert(m_polyTip);

	if (m_pickingHub)
	{
		m_pickingHub->removeListener(this);
	}

	if (m_associatedWin)
	{
		m_associatedWin->displayNewMessage("Polyline tracing [OFF]",
			ccGLWindow::UPPER_CENTER_MESSAGE,
			false,
			2,
			ccGLWindow::MANUAL_SEGMENTATION_MESSAGE);

		m_associatedWin->setUnclosable(false);
		m_associatedWin->removeFromOwnDB(m_polyTip);
		m_associatedWin->setInteractionMode(ccGLWindow::TRANSFORM_CAMERA());
		m_associatedWin->setCursor(Qt::ArrowCursor);
	}

	s_defaultPickingRadius = snapSizeSpinBox->value();
	s_overSamplingCount = oversampleSpinBox->value();

	if (m_dest_prim_group) {
		m_dest_prim_group = nullptr;
	}

	ccOverlayDialog::stop(accepted);
}

void ccTracePolylineTool::updatePolyLineTip(int x, int y, Qt::MouseButtons buttons)
{
	if (!m_associatedWin)
	{
		assert(false);
		return;
	}
	
	if (buttons != Qt::NoButton)
	{
		//nothing to do (just hide the tip)
		if (m_polyTip->isEnabled())
		{
			m_polyTip->setEnabled(false);
			m_associatedWin->redraw(true, false);
		}
		return;
	}

	if (!m_poly3DVertices || m_poly3DVertices->size() == 0)
	{
		//there should be at least one point already picked!
		return;
	}

	if (m_done)
	{
		// when it is done do nothing
		return;
	}

	assert(m_polyTip && m_polyTipVertices && m_polyTipVertices->size() == 2);

	//we replace the last point by the new one
	{
		QPointF pos2D = m_associatedWin->toCenteredGLCoordinates(x, y);
		CCVector3 P2D(	static_cast<PointCoordinateType>(pos2D.x()),
						static_cast<PointCoordinateType>(pos2D.y()),
						0);

		CCVector3* lastP = const_cast<CCVector3*>(m_polyTipVertices->getPointPersistentPtr(1));
		*lastP = P2D;
	}

	//just in case (e.g. if the view has been rotated or zoomed)
	//we also update the first vertex position!
	{
		const CCVector3* P3D = m_poly3DVertices->getPoint(m_poly3DVertices->size() - 1);

		ccGLCameraParameters camera;
		m_associatedWin->getGLCameraParameters(camera);

		CCVector3d A2D;
		camera.project(*P3D, A2D);

		CCVector3* firstP = const_cast<CCVector3*>(m_polyTipVertices->getPointPersistentPtr(0));
		*firstP = CCVector3(static_cast<PointCoordinateType>(A2D.x - camera.viewport[2] / 2), //we convert A2D to centered coordinates (no need to apply high DPI scale or anything!)
							static_cast<PointCoordinateType>(A2D.y - camera.viewport[3] / 2),
							0);

	}

	m_polyTip->setEnabled(true);

	m_associatedWin->redraw(true, false);
}


void ccTracePolylineTool::onItemPicked(const PickedItem& pi)
{
	if (!m_associatedWin)
	{
		assert(false);
		return;
	}

	if (!pi.entity)
	{
		//means that the mouse has been clicked but no point was found!
		return;
	}

	//if the 3D polyline doesn't exist yet, we create it
	if (!m_poly3D || !m_poly3DVertices)
	{
		m_poly3DVertices = new ccPointCloud("Vertices");
		m_poly3DVertices->setEnabled(false);
		m_poly3DVertices->setDisplay(m_associatedWin);

		m_poly3D = new ccPolyline(m_poly3DVertices);
		m_poly3D->setTempColor(ccColor::green);
		m_poly3D->set2DMode(false);
		m_poly3D->addChild(m_poly3DVertices);
		m_poly3D->setWidth(widthSpinBox->value() < 2 ? 0 : widthSpinBox->value()); //'1' is equivalent to the default line size

		ccGenericPointCloud* cloud = ccHObjectCaster::ToGenericPointCloud(pi.entity);
		if (cloud)
		{
			//copy the first clicked entity's global shift & scale
			m_poly3D->setGlobalShift(cloud->getGlobalShift());
			m_poly3D->setGlobalScale(cloud->getGlobalScale());
		}

		m_segmentParams.resize(0); //just in case

		m_associatedWin->addToOwnDB(m_poly3D);
	}

	//try to add one more point
	if (	!m_poly3DVertices->reserve(m_poly3DVertices->size() + 1)
		||	!m_poly3D->reserve(m_poly3DVertices->size() + 1))
	{
		ccLog::Error("Not enough memory");
		return;
	}

	try
	{
		m_segmentParams.reserve(m_segmentParams.size() + 1);
	}
	catch (const std::bad_alloc&)
	{
		ccLog::Error("Not enough memory");
		return;
	}

	m_poly3DVertices->addPoint(pi.P3D);
	m_poly3D->addPointIndex(m_poly3DVertices->size() - 1);
	m_segmentParams.emplace_back(m_associatedWin, pi.clickPoint.x(), pi.clickPoint.y());

	//we replace the first point of the tip by this new point
	{
		QPointF pos2D = m_associatedWin->toCenteredGLCoordinates(pi.clickPoint.x(), pi.clickPoint.y());
		CCVector3 P2D(	static_cast<PointCoordinateType>(pos2D.x()),
						static_cast<PointCoordinateType>(pos2D.y()),
						0);

		CCVector3* firstTipPoint = const_cast<CCVector3*>(m_polyTipVertices->getPointPersistentPtr(0));
		*firstTipPoint = P2D;
		m_polyTip->setEnabled(false); //don't need to display it for now
	}

	m_associatedWin->redraw(false, false);
}

void ccTracePolylineTool::closePolyLine(int, int)
{
	if (!m_poly3D || (QApplication::keyboardModifiers() & Qt::ShiftModifier)) //CTRL + right click = panning
	{
		return;
	}

	unsigned vertCount = m_poly3D->size();
	if (vertCount < 2)
	{
		//discard this polyline
		resetLine();
	}
	else
	{
		//hide the tip
		if (m_polyTip)
		{
			m_polyTip->setEnabled(false);
		}
		//update the GUI
		validButton->setEnabled(true);
		saveToolButton->setEnabled(true);
		resetToolButton->setEnabled(true);
		continueToolButton->setEnabled(true);
		if (m_pickingHub)
		{
			m_pickingHub->removeListener(this);
		}
		m_associatedWin->setPickingMode(ccGLWindow::NO_PICKING); //no more picking
		m_done = true;

		if (m_associatedWin)
		{
			m_associatedWin->redraw(true, false);
		}
	}
}

void ccTracePolylineTool::restart(bool reset)
{
	if (m_poly3D)
	{
		if (reset)
		{
			if (m_associatedWin)
			{
				//discard this polyline
				m_associatedWin->removeFromOwnDB(m_poly3D);
			}
			if (m_polyTip)
			{
				//hide the tip
				m_polyTip->setEnabled(false);
			}

			delete m_poly3D;
			m_segmentParams.resize(0);
			//delete m_poly3DVertices;
			m_poly3D = 0;
			m_poly3DVertices = 0;
		}
		else
		{
			if (m_polyTip)
			{
				//show the tip
				m_polyTip->setEnabled(true);
			}
		}
	}

	//enable picking
	if (m_pickingHub && !m_pickingHub->addListener(this, true/*, true, ccGLWindow::POINT_PICKING*/))
	{
		ccLog::Error("The picking mechanism is already in use. Close the tool using it first.");
	}

	if (m_associatedWin)
	{
		m_associatedWin->redraw(false, false);
	}
	
	validButton->setEnabled(false);
	saveToolButton->setEnabled(false);
	resetToolButton->setEnabled(false);
	continueToolButton->setEnabled(false);
	m_done = false;
}

void ccTracePolylineTool::exportLine()
{
	if (!m_poly3D)
	{
		return;
	}

	// TODO: cannot be as the same line
	if (m_trace_mode == 1 && m_dest_prim_group && m_poly3D->size() < 3) {
		ccLog::Error("trace at least two lines to make a plane");
		return;
	}

	if (m_associatedWin)
	{
		m_associatedWin->removeFromOwnDB(m_poly3D);
	}

	unsigned overSampling = static_cast<unsigned>(oversampleSpinBox->value());
	if (overSampling > 1)
	{
		ccPolyline* poly = polylineOverSampling(overSampling);
		if (poly)
		{
			delete m_poly3D;
			m_segmentParams.resize(0);
			m_poly3DVertices = 0;
			m_poly3D = poly;
		}
	}

	m_poly3D->enableTempColor(false);
	m_poly3D->setDisplay(m_associatedWin); //just in case

	MainWindow* win = MainWindow::TheInstance();
	assert(win);

	if (m_trace_mode == 0) {
		win->addToDB(m_poly3D, win->getCurrentDB());
	}
	else if (m_trace_mode == 1 && m_dest_prim_group) {
		//! close
		CCVector3 last_point = *m_poly3D->getPoint(m_poly3D->size() - 1);
		CCVector3 first_point = *m_poly3D->getPoint(0);
		if ((last_point - first_point).norm2() > 1e-6) {
			//try to add one more point
			if (!m_poly3DVertices->reserve(m_poly3DVertices->size() + 1)
				|| !m_poly3D->reserve(m_poly3DVertices->size() + 1)) {
				ccLog::Error("Not enough memory");
				return;
			}
			m_poly3DVertices->addPoint(first_point);
			m_poly3D->addPointIndex(m_poly3DVertices->size() - 1);
		}
		m_poly3D->setClosed(true);

		//! make a plane
		stocker::Polyline3d export_line = GetPolygonFromPolyline(m_poly3D);
		assert(export_line.size() > 1);
		ccPointCloud* new_plane_cloud =	AddSegmentsAsPlane(export_line, BDDB_DEDUCED_PREFIX, ccColor::Generator::Random());
		if (new_plane_cloud) {
			int biggest = GetMaxNumberExcludeChildPrefix(m_dest_prim_group, BDDB_PLANESEG_PREFIX);
			QString new_plane_name = BDDB_PLANESEG_PREFIX + QString::number(biggest + 1);
			new_plane_cloud->setName(new_plane_name);

			BDBaseHObject* baseObj = GetRootBDBase(m_dest_prim_group);
			if (baseObj) {
				ccPointCloud* todo_point = baseObj->GetTodoPoint(GetBaseName(m_dest_prim_group->getName()));
				if (todo_point) {
					RetrieveAssignedPoints(todo_point, new_plane_cloud, DistanceDoubleSpinBox->value());
				}				
			}

			SetGlobalShiftAndScale(new_plane_cloud);
			m_dest_prim_group->addChild(new_plane_cloud);
			new_plane_cloud->setDisplay_recursive(m_dest_prim_group->getDisplay());
			win->addToDB(new_plane_cloud, win->getCurrentDB(), false, false);
		}
	}

	m_poly3D = 0;
	m_segmentParams.resize(0);
	m_poly3DVertices = 0;

	resetLine(); //to update the GUI
}

void ccTracePolylineTool::apply()
{
	exportLine();

	stop(true);
}

void ccTracePolylineTool::cancel()
{
	resetLine();

	stop(false);
}

void ccTracePolylineTool::onWidthSizeChanged(int width)
{
	if (m_poly3D)
	{
		m_poly3D->setWidth(width);
	}
	if (m_polyTip)
	{
		m_polyTip->setWidth(width);
	}
	
	if (m_associatedWin)
	{
		m_associatedWin->redraw(m_poly3D == 0, false);
	}
}

void ccTracePolylineTool::setTraceMode(int mode, ccHObject* prim_group)
{
	m_trace_mode = mode; 
	switch (m_trace_mode)
	{
	case  0:	// original trace polyline
	{
		setFixedWidth(495);
		label_3->setVisible(true);
		widthSpinBox->setVisible(true);
		label_2->setVisible(true);
		oversampleSpinBox->setVisible(true);
		label->setVisible(true);
		snapSizeSpinBox->setVisible(true);

		label_4->setVisible(false);
		DistanceDoubleSpinBox->setVisible(false);

		m_dest_prim_group = prim_group ? prim_group : nullptr;
		break;
	}
	case  1: // trace polyline and make a plane
	{
		setFixedWidth(380);
		label_3->setVisible(false);
		widthSpinBox->setVisible(false);
		label_2->setVisible(false);
		oversampleSpinBox->setVisible(false);
		label->setVisible(true);
		snapSizeSpinBox->setVisible(true);
		
		label_4->setVisible(true);
		label_4->setText("Distance Threshold");
		DistanceDoubleSpinBox->setVisible(true);

		m_dest_prim_group = prim_group ? prim_group : nullptr;
		break;
	}
	default:
		break;
	}
}