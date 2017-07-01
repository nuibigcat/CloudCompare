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

#ifdef CC_LAS_SUPPORT

#include "LASFilter.h"

//Local
#include "LASOpenDlg.h"

//qCC_db
#include <ccLog.h>
#include <ccPointCloud.h>
#include <ccProgressDialog.h>
#include <ccScalarField.h>
#include "ccColorScalesManager.h"

//CCLib
#include <CCPlatform.h>

//Qt
#include <QFileInfo>
#include <QSharedPointer>
#include <QInputDialog>

//pdal
#include <memory>
#include <pdal/PointTable.hpp>
#include <pdal/PointView.hpp>
#include <pdal/Options.hpp>
#include <pdal/Dimension.hpp>
#include <pdal/io/LasReader.hpp>
#include <pdal/io/LasHeader.hpp>
#include <pdal/io/LasWriter.hpp>
#include <pdal/io/LasVLR.hpp>
#include <pdal/io/BufferReader.hpp>
Q_DECLARE_METATYPE(pdal::SpatialReference)

using namespace pdal::Dimension;

//Qt gui
#include <ui_saveLASFileDlg.h>

//System
#include <string.h>
#include <iostream>				// std::cout

static const char s_LAS_SRS_Key[] = "LAS.spatialReference.nosave"; //DGM: added the '.nosave' suffix because this custom type can't be streamed properly

//! LAS Save dialog
class LASSaveDlg : public QDialog, public Ui::SaveLASFileDialog
{
public:
	explicit LASSaveDlg(QWidget* parent = 0)
		: QDialog(parent)
		, Ui::SaveLASFileDialog()
	{
		setupUi(this);
	}
};

bool LASFilter::canLoadExtension(QString upperCaseExt) const
{
	return (	upperCaseExt == "LAS"
			||	upperCaseExt == "LAZ");
}

bool LASFilter::canSave(CC_CLASS_ENUM type, bool& multiple, bool& exclusive) const
{
	if (type == CC_TYPES::POINT_CLOUD)
	{
		multiple = false;
		exclusive = true;
		return true;
	}
	return false;
}


//! Custom ("Extra bytes") field
struct ExtraLasField : LasField
{
    //! Default constructor
    ExtraLasField(QString name, pdal::Dimension::Id id, double defaultVal = 0, double min = 0.0, double max = -1.0)
        : LasField(LAS_EXTRA,defaultVal,min,max)
        , fieldName(name)
        , pdalId(id)
        , scale(1.0)
        , offset(0.0)
    {}

    //reimplemented from LasField
    virtual inline QString getName() const	{ return fieldName; }

    QString fieldName;
    pdal::Dimension::Id pdalId;
    double scale;
    double offset;
};

//! Semi persistent save dialog
QSharedPointer<LASSaveDlg> s_saveDlg(0);


CC_FILE_ERROR LASFilter::saveToFile(ccHObject* entity, QString filename, SaveParameters& parameters)
  {
    if (!entity || filename.isEmpty())
        return CC_FERR_BAD_ARGUMENT;

    ccGenericPointCloud* theCloud = ccHObjectCaster::ToGenericPointCloud(entity);
    if (!theCloud)
    {
        ccLog::Warning("[LAS] This filter can only save one cloud at a time");
        return CC_FERR_BAD_ENTITY_TYPE;
    }

    unsigned numberOfPoints = theCloud->size();
    if (numberOfPoints == 0)
    {
        ccLog::Warning("[LAS] Cloud is empty!");
        return CC_FERR_NO_SAVE;
    }

    //colors
    bool hasColors = theCloud->hasColors();

    //additional fields (as scalar fields)
    std::vector<LasField> fieldsToSave;

    if (theCloud->isA(CC_TYPES::POINT_CLOUD))
    {
        ccPointCloud* pc = static_cast<ccPointCloud*>(theCloud);

        //match cloud SFs with official LASfields
        LasField::GetLASFields(pc, fieldsToSave);
    }

    //progress dialog
    QScopedPointer<ccProgressDialog> pDlg(0);
    if (parameters.parentWidget)
    {
        pDlg.reset(new ccProgressDialog(true, parameters.parentWidget)); //cancel available
        pDlg->setMethodTitle(QObject::tr("Save LAS file"));
        pDlg->setInfo(QObject::tr("Points: %1").arg(numberOfPoints));
        pDlg->start();
    }
    CCLib::NormalizedProgress nProgress(pDlg.data(), numberOfPoints);

    pdal::LasWriter writer;
    pdal::Options writerOptions;
    pdal::PointTable table;
    pdal::BufferReader bufferReader;



    CCVector3d bbMin, bbMax;
    if (!theCloud->getGlobalBB(bbMin, bbMax))
    {
        return CC_FERR_NO_SAVE;
    }
    //Set offset & scale, as points will be stored as boost::int32_t values (between 0 and 4294967296)
    //int_value = (double_value-offset)/scale
    writerOptions.add("offset_x", bbMin.x);
    writerOptions.add("offset_y", bbMin.y);
    writerOptions.add("offset_z", bbMin.z);
    CCVector3d diag = bbMax - bbMin;


    //let the user choose between the original scale and the 'optimal' one (for accuracy, not for compression ;)
    bool hasScaleMetaData = false;
    CCVector3d lasScale(0, 0, 0);
    lasScale.x = theCloud->getMetaData(LAS_SCALE_X_META_DATA).toDouble(&hasScaleMetaData);
    if (hasScaleMetaData)
    {
        lasScale.y = theCloud->getMetaData(LAS_SCALE_Y_META_DATA).toDouble(&hasScaleMetaData);
        if (hasScaleMetaData)
        {
            lasScale.z = theCloud->getMetaData(LAS_SCALE_Z_META_DATA).toDouble(&hasScaleMetaData);
        }
    }

    //optimal scale (for accuracy) --> 1e-9 because the maximum integer is roughly +/-2e+9
    CCVector3d optimalScale(1.0e-9 * std::max<double>(diag.x, ZERO_TOLERANCE),
                            1.0e-9 * std::max<double>(diag.y, ZERO_TOLERANCE),
                            1.0e-9 * std::max<double>(diag.z, ZERO_TOLERANCE));

    if (parameters.alwaysDisplaySaveDialog)
    {
        if (!s_saveDlg)
            s_saveDlg = QSharedPointer<LASSaveDlg>(new LASSaveDlg(0));
        s_saveDlg->bestAccuracyLabel->setText(QString("(%1, %2, %3)").arg(optimalScale.x).arg(optimalScale.y).arg(optimalScale.z));

        if (hasScaleMetaData)
        {
            s_saveDlg->origAccuracyLabel->setText(QString("(%1, %2, %3)").arg(lasScale.x).arg(lasScale.y).arg(lasScale.z));
        }
        else
        {
            s_saveDlg->origAccuracyLabel->setText("none");
            if (s_saveDlg->origRadioButton->isChecked())
                s_saveDlg->bestRadioButton->setChecked(true);
            s_saveDlg->origRadioButton->setEnabled(false);
        }

        s_saveDlg->exec();

        if (s_saveDlg->bestRadioButton->isChecked())
        {
            lasScale = optimalScale;
        }
        else if (s_saveDlg->customRadioButton->isChecked())
        {
            double s = s_saveDlg->customScaleDoubleSpinBox->value();
            lasScale = CCVector3d(s, s, s);
        }
    }
    else if (!hasScaleMetaData)
    {
        lasScale = optimalScale;
    }
    writerOptions.add("scale_x", lasScale.x);
    writerOptions.add("scale_y", lasScale.y);
    writerOptions.add("scale_z", lasScale.z);


//    if (theCloud->hasMetaData(s_LAS_SRS_Key))
//    {
//        //restore the SRS if possible
//        QString srs = theCloud->getMetaData(s_LAS_SRS_Key).value<QString>();
//        writerOptions.add("a_srs", srs.toStdString());
//    }


    pdal::Dimension::IdList dimsToSave;
    for (LasField &lasField: fieldsToSave)
    {
        dimsToSave.push_back(pdal::Dimension::id(lasField.getName().toStdString()));
        table.layout()->registerDim(pdal::Dimension::id(lasField.getName().toStdString()));
    }
    if (hasColors)
    {
        table.layout()->registerDim(Id::Red);
        table.layout()->registerDim(Id::Green);
        table.layout()->registerDim(Id::Blue);
    }

    pdal::Dimension::IdList dims = table.layout()->dims();
    for (auto &dimId: dims)
    {
         std::cerr << "dim: " << pdal::Dimension::name(dimId) << std::endl;
    }
    table.layout()->registerDim(Id::X);
    table.layout()->registerDim(Id::Y);
    table.layout()->registerDim(Id::Z);

    pdal::PointViewPtr point_view(new pdal::PointView(table));

    for (unsigned i = 0; i < numberOfPoints; ++i)
    {
        const CCVector3* P = theCloud->getPoint(i);
        {
            CCVector3d Pglobal = theCloud->toGlobal3d<PointCoordinateType>(*P);
            point_view->setField(Id::X, i, Pglobal.x);
            point_view->setField(Id::Y, i, Pglobal.y);
            point_view->setField(Id::Z, i, Pglobal.z);
        }
        nProgress.oneStep();


        if (hasColors)
        {
            //DGM: LAS colors are stored on 16 bits!
            const ColorCompType* rgb = theCloud->getPointColor(i);
            point_view->setField(Id::Red,   i, static_cast<uint16_t>(rgb[0]) << 8);
            point_view->setField(Id::Green, i, static_cast<uint16_t>(rgb[1]) << 8);
            point_view->setField(Id::Blue,  i, static_cast<uint16_t>(rgb[2]) << 8);
        }

        //additional fields
        for (std::vector<LasField>::const_iterator it = fieldsToSave.begin(); it != fieldsToSave.end(); ++it)
        {
            assert(it->sf);
            switch(it->type)
            {
            case LAS_X:
            case LAS_Y:
            case LAS_Z:
                assert(false);
                break;
            case LAS_INTENSITY:
                point_view->setField(Id::Intensity, i, it->sf->getValue(i));
                break;
            case LAS_RETURN_NUMBER:
                point_view->setField(Id::ReturnNumber, i, it->sf->getValue(i));
                break;
            case LAS_NUMBER_OF_RETURNS:
                point_view->setField(Id::NumberOfReturns, i, it->sf->getValue(i));
                break;
            case LAS_SCAN_DIRECTION:
                point_view->setField(Id::ScanDirectionFlag, i, it->sf->getValue(i));
                break;
            case LAS_FLIGHT_LINE_EDGE:
                point_view->setField(Id::EdgeOfFlightLine, i, it->sf->getValue(i));
                break;
            case LAS_CLASSIFICATION:
                point_view->setField(Id::Classification, i, it->sf->getValue(i));
                break;
            case LAS_SCAN_ANGLE_RANK:
                point_view->setField(Id::ScanAngleRank, i, it->sf->getValue(i));
                break;
            case LAS_USER_DATA:
                point_view->setField(Id::UserData, i, it->sf->getValue(i));
                break;
            case LAS_POINT_SOURCE_ID:
                point_view->setField(Id::PointSourceId, i, it->sf->getValue(i));
                break;
            case LAS_RED:
            case LAS_GREEN:
            case LAS_BLUE:
                assert(false);
                break;
            case LAS_TIME:
                point_view->setField(Id::GpsTime, i, it->sf->getValue(i) + it->sf->getGlobalShift());
                break;
//            case LAS_CLASSIF_VALUE:
//                classif.SetClass(static_cast<boost::uint32_t>(it->sf->getValue(i)));
//                break;
//            case LAS_CLASSIF_SYNTHETIC:
//                classif.SetSynthetic(static_cast<boost::uint32_t>(it->sf->getValue(i)));
//                break;
//            case LAS_CLASSIF_KEYPOINT:
//                classif.SetKeyPoint(static_cast<boost::uint32_t>(it->sf->getValue(i)));
//                break;
//            case LAS_CLASSIF_WITHHELD:
//                classif.SetWithheld(static_cast<boost::uint32_t>(it->sf->getValue(i)));
//                break;
            case LAS_INVALID:
            default:
                assert(false);
                break;
            }
        }
    }



    // spatial reference
    writerOptions.add("filename", filename.toStdString());


    dims = point_view->dims();
    for (auto &dimId: dims)
    {
         std::cerr << "dim: " << pdal::Dimension::name(dimId) << std::endl;
    }

    try
    {
        bufferReader.addView(point_view);
        writer.setInput(bufferReader);
        writer.setOptions(writerOptions);
        writer.prepare(table);
        writer.execute(table);
    }
    catch (const pdal::pdal_error& e)
    {
        ccLog::Error(QString("PDAL exception '%1'").arg(e.what()));
        return CC_FERR_THIRD_PARTY_LIB_EXCEPTION;
    }
    catch (...)
    {
        return CC_FERR_THIRD_PARTY_LIB_FAILURE;
    }

    return CC_FERR_NO_ERROR;
  }


QSharedPointer<LASOpenDlg> s_lasOpenDlg(0);

//! Structure describing the current tiling process
struct PDALTilingStruct
{
    PDALTilingStruct()
        : w(1)
        , h(1)
        , X(0)
        , Y(1)
        , Z(2)
    {}

    ~PDALTilingStruct()
    {
    }

    inline size_t tileCount() const { return tilePointViews.size(); }

    bool init(	unsigned width,
                unsigned height,
                unsigned Zdim,
                QString absoluteBaseFilename,
                const CCVector3d& bbMin,
                const CCVector3d& bbMax,
                const pdal::PointTableRef table)
    {
        //init tiling dimensions
        assert(Zdim < 3);
        Z = Zdim;
        X = (Z == 2 ? 0 : Z+1);
        Y = (X == 2 ? 0 : X+1);

        bbMinCorner = bbMin;
        tileDiag = bbMax - bbMin;
        tileDiag.u[X] /= width;
        tileDiag.u[Y] /= height;


        unsigned count = width * height;
        try
        {
            tilePointViews.resize(count);
            fileNames.resize(count);
        }
        catch (const std::bad_alloc&)
        {
            //not enough memory
            return false;
        }

        w = width;
        h = height;

        //File extension
//        QString ext = (header.Compressed() ? "laz" : "las");

        for (unsigned i = 0; i < width; ++i)
        {
            for (unsigned j = 0; j < height; ++j)
            {
                unsigned ii = index(i, j);
                //TODO: Don't forget to change the ext to be able to write .laz
                QString filename = absoluteBaseFilename + QString("_%1_%2.%3").arg(QString::number(i), QString::number(j), QString("las"));

                fileNames[ii] = filename;
                tilePointViews[ii] = pdal::PointViewPtr(new pdal::PointView(table));
            }
        }

        return true;
    }

    void addPoint(const pdal::PointViewPtr buffer, unsigned pointIndex)
    {
        //determine the right tile
        CCVector3d Prel = CCVector3d(buffer->getFieldAs<double>(Id::X, pointIndex),
                                     buffer->getFieldAs<double>(Id::Y, pointIndex),
                                     buffer->getFieldAs<double>(Id::Z, pointIndex));
        Prel -= bbMinCorner;
        int ii = static_cast<int>(floor(Prel.u[X] / tileDiag.u[X]));
        int ji = static_cast<int>(floor(Prel.u[Y] / tileDiag.u[Y]));
        unsigned i = std::min( static_cast<unsigned>(std::max(ii, 0)), w-1);
        unsigned j = std::min( static_cast<unsigned>(std::max(ji, 0)), h-1);
        pdal::PointViewPtr outputView = tilePointViews[index(i,j)];
        outputView->appendPoint(*buffer, pointIndex);
    }

    void writeAll()
    {

        for (unsigned i = 0; i < tilePointViews.size(); ++i)
        {
            pdal::LasWriter writer;
            pdal::Options writerOptions;
            pdal::PointTable table;
            pdal::BufferReader bufferReader;

            writerOptions.add("filename", fileNames[i].toStdString());
            try
            {
                bufferReader.addView(tilePointViews[i]);
                writer.setInput(bufferReader);
                writer.setOptions(writerOptions);
                writer.prepare(table);
                writer.execute(table);
            }
            catch (const pdal::pdal_error& e)
            {
                ccLog::Error(QString("PDAL exception '%1'").arg(e.what()));
            }
        }
    }

protected:

    inline unsigned index(unsigned i, unsigned j) const { return i + j * w; }

    unsigned w, h;
    unsigned X, Y, Z;
    CCVector3d bbMinCorner, tileDiag;
    std::vector<pdal::PointViewPtr> tilePointViews;
    std::vector<QString> fileNames;
};

CC_FILE_ERROR LASFilter::loadFile(QString filename, ccHObject& container, LoadParameters& parameters)
{
    pdal::Options las_opts;
    las_opts.add("filename", filename.toStdString());

    pdal::PointTable table;
    pdal::LasReader lasReader;
    pdal::LasHeader lasHeader;
    pdal::PointViewSet pointViewSet;
    pdal::PointViewPtr pointView;
    pdal::Dimension::IdList dims;


    try
    {
        lasReader.setOptions(las_opts);
        lasReader.prepare(table);
        pointViewSet = lasReader.execute(table);
        pointView = *pointViewSet.begin();
        dims = pointView->dims();
        lasHeader = lasReader.header();
    }
    catch (const pdal::pdal_error& e)
    {
        ccLog::Error(QString("PDAL exception '%1'").arg(e.what()));
        return CC_FERR_THIRD_PARTY_LIB_EXCEPTION;
    }
    catch (...)
    {
        return CC_FERR_THIRD_PARTY_LIB_FAILURE;
    }

    CCVector3d bbMin(lasHeader.minX(), lasHeader.minY(), lasHeader.minZ());
    CCVector3d bbMax(lasHeader.maxX(), lasHeader.maxY(), lasHeader.maxZ());

    CCVector3d lasScale =  CCVector3d(lasHeader.scaleX(), lasHeader.scaleY(), lasHeader.scaleZ());
    CCVector3d lasShift = -CCVector3d(lasHeader.offsetX(), lasHeader.offsetY(), lasHeader.offsetZ());

    unsigned int nbOfPoints = lasHeader.pointCount();
    if (nbOfPoints == 0)
    {
        //strange file ;)
        return CC_FERR_NO_LOAD;
    }

    std::vector<std::string> dimensions;
    std::vector<std::string> extraDimensions;
    IdList extraDimensionsIds;
    for (auto &dimId: dims)
    {
        // Extra dimensions names are only known by the point_view as
        // they are not standard
        if (pdal::Dimension::name(dimId).empty())
        {
            extraDimensions.push_back(pointView->dimName(dimId));
            extraDimensionsIds.push_back(dimId);
        }
        else
        {
            dimensions.push_back(pdal::Dimension::name(dimId));
        }
    }

    if (!s_lasOpenDlg)
    {
        s_lasOpenDlg = QSharedPointer<LASOpenDlg>(new LASOpenDlg());
    }
    s_lasOpenDlg->setDimensions(dimensions);
    s_lasOpenDlg->clearEVLRs();
    s_lasOpenDlg->setInfos(filename, nbOfPoints, bbMin, bbMax);

    for (std::string &extraDimension: extraDimensions)
    {
        s_lasOpenDlg->addEVLR(QString("%1 (%2)").arg(QString::fromStdString(extraDimension)).arg(QString::fromStdString("")));
    }

    if (parameters.alwaysDisplayLoadDialog && !s_lasOpenDlg->autoSkipMode() && !s_lasOpenDlg->exec())
    {
        return CC_FERR_CANCELED_BY_USER;
    }

    bool ignoreDefaultFields = s_lasOpenDlg->ignoreDefaultFieldsCheckBox->isChecked();

    PDALTilingStruct tiler;
    bool tiling = s_lasOpenDlg->tileGroupBox->isChecked();
    if (tiling)
    {
        // tiling (vertical) dimension
        unsigned vertDim = 2;
        switch (s_lasOpenDlg->tileDimComboBox->currentIndex())
        {
        case 0: //XY
            vertDim = 2;
            break;
        case 1: //XZ
            vertDim = 1;
            break;
        case 2: //YZ
            vertDim = 0;
            break;
        default:
            assert(false);
            break;
        }

        unsigned w = static_cast<unsigned>(s_lasOpenDlg->wTileSpinBox->value());
        unsigned h = static_cast<unsigned>(s_lasOpenDlg->hTileSpinBox->value());

        QString outputBaseName = s_lasOpenDlg->outputPathLineEdit->text() + "/" + QFileInfo(filename).baseName();
        if (!tiler.init(w, h, vertDim, outputBaseName, bbMin, bbMax, table))
        {
            return CC_FERR_NOT_ENOUGH_MEMORY;
        }
    }

    unsigned short rgbColorMask[3] = {0, 0, 0};
    if (s_lasOpenDlg->doLoad(LAS_RED))
        rgbColorMask[0] = (~0);
    if (s_lasOpenDlg->doLoad(LAS_GREEN))
        rgbColorMask[1] = (~0);
    if (s_lasOpenDlg->doLoad(LAS_BLUE))
        rgbColorMask[2] = (~0);
    bool loadColor = (rgbColorMask[0] || rgbColorMask[1] || rgbColorMask[2]);

    CCVector3d Pshift(0, 0, 0);

    //by default we read colors as triplets of 8 bits integers but we might dynamically change this
    //if we encounter values using 16 bits (16 bits is the standard!)
    unsigned char colorCompBitShift = 0;
    bool forced8bitRgbMode = s_lasOpenDlg->forced8bitRgbMode();
    ColorCompType rgb[3] = { 0, 0, 0 };


    std::vector< LasField::Shared > fieldsToLoad;
    IdList extraFieldsToLoad;


    //first point: check for 'big' coordinates
    CCVector3d P(pointView->getFieldAs<double>(Id::X, 0),
                 pointView->getFieldAs<double>(Id::Y, 0),
                 pointView->getFieldAs<double>(Id::Z, 0));
    //backup input global parameters
    ccGlobalShiftManager::Mode csModeBackup = parameters.shiftHandlingMode;
    bool useLasShift = false;
    //set the LAS shift as default shift (if none was provided)
    if (lasShift.norm2() != 0 && (!parameters.coordinatesShiftEnabled || !*parameters.coordinatesShiftEnabled))
    {
        useLasShift = true;
        Pshift = lasShift;
        if (	csModeBackup != ccGlobalShiftManager::NO_DIALOG
                &&	csModeBackup != ccGlobalShiftManager::NO_DIALOG_AUTO_SHIFT)
        {
            parameters.shiftHandlingMode = ccGlobalShiftManager::ALWAYS_DISPLAY_DIALOG;
        }
    }

    //restore previous parameters
    parameters.shiftHandlingMode = csModeBackup;
    QScopedPointer<ccProgressDialog> pDlg(0);
    if (parameters.parentWidget)
    {
        pDlg.reset(new ccProgressDialog(true, parameters.parentWidget)); //cancel available
        pDlg->setMethodTitle(QObject::tr("Open LAS file"));
        pDlg->setInfo(QObject::tr("Points: %1").arg(nbOfPoints));
        pDlg->start();
    }
    CCLib::NormalizedProgress nProgress(pDlg.data(), nbOfPoints);

    unsigned int fileChunkPos = 0;
    unsigned int fileChunkSize = 0;

    ccPointCloud* loadedCloud = 0;

    for (unsigned i = 0; i < extraDimensionsIds.size(); ++i)
    {
        if (s_lasOpenDlg->doLoadEVLR(i))
            extraFieldsToLoad.push_back(extraDimensionsIds[i]);
    }

    for (pdal::PointId idx = 0; idx < pointView->size()+1; ++idx)
    {
        // special operation: tiling mode
        if (tiling && idx < pointView->size())
        {
            tiler.addPoint(pointView, idx);
            nProgress.oneStep();
            continue;
        }
        if (idx == pointView->size() || idx == fileChunkPos + fileChunkSize)
        {

            if (loadedCloud)
            {
                if (loadedCloud->size())
                {

                    bool thisChunkHasColors = loadedCloud->hasColors();
                    loadedCloud->showColors(thisChunkHasColors);
                    if (loadColor && !thisChunkHasColors)
                    {
                        ccLog::Warning("[LAS] Color field was all black! We ignored it...");
                    }

                    while (!fieldsToLoad.empty())
                    {
                        LasField::Shared& field = fieldsToLoad.back();
                        if (field && field->sf)
                        {
                            field->sf->computeMinAndMax();

                            if (	field->type == LAS_CLASSIFICATION
                                    ||	field->type == LAS_CLASSIF_VALUE
                                    ||	field->type == LAS_CLASSIF_SYNTHETIC
                                    ||	field->type == LAS_CLASSIF_KEYPOINT
                                    ||	field->type == LAS_CLASSIF_WITHHELD
                                    ||	field->type == LAS_RETURN_NUMBER
                                    ||	field->type == LAS_NUMBER_OF_RETURNS)
                            {
                                int cMin = static_cast<int>(field->sf->getMin());
                                int cMax = static_cast<int>(field->sf->getMax());
                                field->sf->setColorRampSteps(std::min<int>(cMax - cMin + 1, 256));
                                //classifSF->setMinSaturation(cMin);

                            }
                            else if (field->type == LAS_INTENSITY)
                            {
                                field->sf->setColorScale(ccColorScalesManager::GetDefaultScale(ccColorScalesManager::GREY));
                            }

                            int sfIndex = loadedCloud->addScalarField(field->sf);
                            if (!loadedCloud->hasDisplayedScalarField())
                            {
                                loadedCloud->setCurrentDisplayedScalarField(sfIndex);
                                loadedCloud->showSF(!thisChunkHasColors);
                            }
                            field->sf->release();
                            field->sf = 0;
                        }
                        else
                        {
                            ccLog::Warning(QString("[LAS] All '%1' values were the same (%2)! We ignored them...").arg(field->type == LAS_EXTRA ? field->getName() : QString(LAS_FIELD_NAMES[field->type])).arg(field->firstValue));
                        }

                        fieldsToLoad.pop_back();
                    }


                    // if we had reserved too much memory
                    if (loadedCloud->size() < loadedCloud->capacity())
                    {
                        loadedCloud->resize(loadedCloud->size());
                    }

                    QString chunkName("unnamed - Cloud");
                    unsigned n = container.getChildrenNumber();
                    if (n != 0)
                    {
                        if (n == 1)
                        {
                            container.getChild(0)->setName(chunkName + QString(" #1"));
                        }
                        chunkName += QString(" #%1").arg(n + 1);
                    }
                    loadedCloud->setName(chunkName);

                    loadedCloud->setMetaData(LAS_SCALE_X_META_DATA, QVariant(lasScale.x));
                    loadedCloud->setMetaData(LAS_SCALE_Y_META_DATA, QVariant(lasScale.y));
                    loadedCloud->setMetaData(LAS_SCALE_Z_META_DATA, QVariant(lasScale.z));

                    container.addChild(loadedCloud);
                    loadedCloud = 0;
                }
                else
                {
                    //empty cloud?!
                    delete loadedCloud;
                    loadedCloud = 0;
                }
            }

            if (idx == pointView->size())
            {
                break;
            }
            // otherwise, we must create a new cloud
            fileChunkPos = idx;
            unsigned int pointsToRead = static_cast<unsigned int>(pointView->size()) - idx;
            fileChunkSize = std::min(pointsToRead, CC_MAX_NUMBER_OF_POINTS_PER_CLOUD);
            loadedCloud = new ccPointCloud();

            if (!loadedCloud->reserveThePointsTable(fileChunkSize))
            {
                ccLog::Warning("[LAS] Not enough memory!");
                delete loadedCloud;
                return CC_FERR_NOT_ENOUGH_MEMORY;
            }
            loadedCloud->setGlobalShift(Pshift);

            //save the Spatial reference as meta-data
            loadedCloud->setMetaData(s_LAS_SRS_Key, QVariant::fromValue(lasHeader.srs()));

            //DGM: from now on, we only enable scalar fields when we detect a valid value!
            if (s_lasOpenDlg->doLoad(LAS_CLASSIFICATION))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_CLASSIFICATION, 0, 0, 255))); //unsigned char: between 0 and 255
            if (s_lasOpenDlg->doLoad(LAS_CLASSIF_VALUE))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_CLASSIF_VALUE, 0, 0, 31))); //5 bits: between 0 and 31
            if (s_lasOpenDlg->doLoad(LAS_CLASSIF_SYNTHETIC))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_CLASSIF_SYNTHETIC, 0, 0, 1))); //1 bit: 0 or 1
            if (s_lasOpenDlg->doLoad(LAS_CLASSIF_KEYPOINT))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_CLASSIF_KEYPOINT, 0, 0, 1))); //1 bit: 0 or 1
            if (s_lasOpenDlg->doLoad(LAS_CLASSIF_WITHHELD))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_CLASSIF_WITHHELD, 0, 0, 1))); //1 bit: 0 or 1
            if (s_lasOpenDlg->doLoad(LAS_INTENSITY))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_INTENSITY, 0, 0, 65535))); //16 bits: between 0 and 65536
            if (s_lasOpenDlg->doLoad(LAS_TIME))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_TIME, 0, 0, -1.0))); //8 bytes (double) --> we use global shift!
            if (s_lasOpenDlg->doLoad(LAS_RETURN_NUMBER))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_RETURN_NUMBER, 1, 1, 7))); //3 bits: between 1 and 7
            if (s_lasOpenDlg->doLoad(LAS_NUMBER_OF_RETURNS))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_NUMBER_OF_RETURNS, 1, 1, 7))); //3 bits: between 1 and 7
            if (s_lasOpenDlg->doLoad(LAS_SCAN_DIRECTION))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_SCAN_DIRECTION, 0, 0, 1))); //1 bit: 0 or 1
            if (s_lasOpenDlg->doLoad(LAS_FLIGHT_LINE_EDGE))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_FLIGHT_LINE_EDGE, 0, 0, 1))); //1 bit: 0 or 1
            if (s_lasOpenDlg->doLoad(LAS_SCAN_ANGLE_RANK))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_SCAN_ANGLE_RANK, 0, -90, 90))); //signed char: between -90 and +90
            if (s_lasOpenDlg->doLoad(LAS_USER_DATA))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_USER_DATA, 0, 0, 255))); //unsigned char: between 0 and 255
            if (s_lasOpenDlg->doLoad(LAS_POINT_SOURCE_ID))
                fieldsToLoad.push_back(LasField::Shared(new LasField(LAS_POINT_SOURCE_ID, 0, 0, 65535))); //16 bits: between 0 and 65536

            //extra fields
            for (Id &extraFieldId: extraFieldsToLoad)
            {
                QString name = QString::fromStdString(pointView->dimName(extraFieldId));
                ExtraLasField *eField = new ExtraLasField(name, extraFieldId);
                fieldsToLoad.push_back(LasField::Shared(eField));
            }
        }



        CCVector3 P(static_cast<PointCoordinateType>(pointView->getFieldAs<double>(Id::X, idx) + Pshift.x),
                    static_cast<PointCoordinateType>(pointView->getFieldAs<double>(Id::Y, idx) + Pshift.y),
                    static_cast<PointCoordinateType>(pointView->getFieldAs<double>(Id::Z, idx) + Pshift.z));
        loadedCloud->addPoint(P);


        if (loadColor)
        {
            unsigned short red = pointView->getFieldAs<unsigned short>(Id::Red, idx) & rgbColorMask[0];
            unsigned short green = pointView->getFieldAs<unsigned short>(Id::Green, idx) & rgbColorMask[1];
            unsigned short blue = pointView->getFieldAs<unsigned short>(Id::Blue, idx) & rgbColorMask[2];

            // if we don't have reserved a color field yet, we check that color is not black
            bool pushColor = true;
            if (!loadedCloud->hasColors())
            {
                if (red || green || blue)
                {
                    if (loadedCloud->reserveTheRGBTable())
                    {
                        // we must set the color (black) of all previously skipped points
                        for (int i = 0; i < loadedCloud->size() - 1; ++i)
                        {
                            loadedCloud->addRGBColor(ccColor::black.rgba);
                        }
                    }
                    else
                    {
                        ccLog::Warning("[LAS]: Not enough memory, color field will be ignored!");
                        loadColor = false; //no need to retry with the other chunks anyway
                        pushColor = false;
                    }
                }
                else //otherwise we ignore it for the moment (we'll add it later if necessary)
                {
                    pushColor = false;
                }
            }
            if (pushColor)
            {
                //we test if the color components are on 16 bits (standard) or only on 8 bits (it happens ;)
                if (!forced8bitRgbMode && colorCompBitShift == 0)
                {
                    if (   (red   & 0xFF00)
                        || (green  & 0xFF00)
                        || (blue & 0xFF00))
                    {
                        //the color components are on 16 bits!
                        ccLog::Print("[LAS] Color components are coded on 16 bits");
                        colorCompBitShift = 8;
                        //we fix all the previously read colors
                        for (unsigned i = 0; i < loadedCloud->size() - 1; ++i)
                        {
                            loadedCloud->setPointColor(i, ccColor::black.rgba); //255 >> 8 = 0!
                        }
                    }
                }
                rgb[0] = static_cast<ColorCompType>(red   >> colorCompBitShift);
                rgb[1] = static_cast<ColorCompType>(green >> colorCompBitShift);
                rgb[2] = static_cast<ColorCompType>(blue  >> colorCompBitShift);

                loadedCloud->addRGBColor(rgb);
            }
        }

        // additional fields
        for (std::vector<LasField::Shared>::iterator it = fieldsToLoad.begin(); it != fieldsToLoad.end(); ++it)
        {
            LasField::Shared field = *it;

            double value = 0.0;
            switch (field->type)
            {
            case LAS_INTENSITY:
                value = pointView->getFieldAs<double>(Id::Intensity, idx);
                break;
            case LAS_RETURN_NUMBER:
                value = pointView->getFieldAs<double>(Id::ReturnNumber, idx);
                break;
            case LAS_NUMBER_OF_RETURNS:
                value = pointView->getFieldAs<double>(Id::NumberOfReturns, idx);
                break;
            case LAS_SCAN_DIRECTION:
                value = pointView->getFieldAs<double>(Id::ScanDirectionFlag, idx);
                break;
            case LAS_FLIGHT_LINE_EDGE:
                value = pointView->getFieldAs<double>(Id::EdgeOfFlightLine, idx);
                break;
            case LAS_CLASSIFICATION:
                value = pointView->getFieldAs<double>(Id::Classification, idx);
                break;
            case LAS_SCAN_ANGLE_RANK:
                value = pointView->getFieldAs<double>(Id::ScanAngleRank, idx);
                break;
            case LAS_USER_DATA:
                value = pointView->getFieldAs<double>(Id::UserData, idx);
                break;
            case LAS_POINT_SOURCE_ID:
                value = pointView->getFieldAs<double>(Id::PointSourceId, idx);
                break;
            case LAS_EXTRA:
            {
                ExtraLasField* extraField = static_cast<ExtraLasField*>((*it).data());
                value = pointView->getFieldAs<double>(extraField->pdalId, idx);
                break;
            }
            case LAS_TIME:
                value = pointView->getFieldAs<double>(Id::GpsTime, idx);
                if (field->sf)
                {
                    //shift time values (so as to avoid losing accuracy)
                    value -= field->sf->getGlobalShift();
                }
                break;
            case LAS_CLASSIF_VALUE:
                value = pointView->getFieldAs<int>(Id::Classification, idx); //5 bits
                break;
            case LAS_CLASSIF_SYNTHETIC:
                value = (pointView->getFieldAs<int>(Id::ClassFlags, idx) & 1); //bit #1
                break;
            case LAS_CLASSIF_KEYPOINT:
                value = (pointView->getFieldAs<int>(Id::ClassFlags, idx) & 2); //bit #2
                break;
            case LAS_CLASSIF_WITHHELD:
                value = (pointView->getFieldAs<int>(Id::Classification, idx) & 4); //bit #3
                break;
             // Overlap flag is the 4 bit (new in las 1.4)
            default:
                //ignored
                assert(false);
                continue;
            }
            if (field->sf)
            {

                ScalarType s = static_cast<ScalarType>(value);
                field->sf->addElement(s);
            }
            else
            {
                //first point? we track its value
                if (loadedCloud->size() == 1)
                {
                    field->firstValue = value;
                }
                if (	!ignoreDefaultFields
                        ||	value != field->firstValue
                        ||	(field->firstValue != field->defaultValue && field->firstValue >= field->minValue))
                {
                    field->sf = new ccScalarField(qPrintable(field->getName()));
                    if (field->sf->reserve(fileChunkSize))
                    {
                        field->sf->link();
                        if (field->type == LAS_TIME)
                        {
                            //we use the first value as 'global shift' (otherwise we will lose accuracy)
                            field->sf->setGlobalShift(field->firstValue);
                            value -= field->firstValue;
                            ccLog::Warning("[LAS] Time SF has been shifted to prevent a loss of accuracy (%.2f)",field->firstValue);
                            field->firstValue = 0;
                        }

                        for (int i = 0; i < loadedCloud->size() - 1; ++i) {
                            field->sf->addElement(static_cast<ScalarType>(field->defaultValue));
                        }
                        ScalarType s = static_cast<ScalarType>(value);
                        field->sf->addElement(s);
                    }
                    else
                    {
                        ccLog::Warning(QString("[LAS] Not enough memory: '%1' field will be ignored!").arg(LAS_FIELD_NAMES[field->type]));
                        field->sf->release();
                        field->sf = 0;
                    }
                }

            }

        }
        nProgress.oneStep();
    }

    // Now the tiler will actually write the points
    if(tiling)
    {
        tiler.writeAll();
    }
    return CC_FERR_NO_ERROR;
}

#endif
