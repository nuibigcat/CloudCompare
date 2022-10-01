#pragma once

//##########################################################################
//#                                                                        #
//#                CLOUDCOMPARE PLUGIN: LAS-IO Plugin                      #
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
//#                   COPYRIGHT: Thomas Montaigu                           #
//#                                                                        #
//##########################################################################

#include <QDialog>

#include <CCGeom.h>

#include "LasDetails.h"
#include "LasScalarField.h"
#include "LasExtraScalarField.h"
#include "ui_lassavedialog.h"

class QStringListModel;
class ccScalarField;
class ccPointCloud;

class MappingLabel;

/// This dialog is responsible for presenting to the user
/// the different options when saving a point cloud to a LAS/LAZ file.
class LasSaveDialog : public QDialog, public Ui::LASSaveDialog
{
    Q_OBJECT

  public:
    explicit LasSaveDialog(ccPointCloud *cloud, QWidget *parent = nullptr);

    void setVersionAndPointFormat(const QString &version, unsigned int pointFormat);
    /// Set scale that would offer the user the best precision
    void setOptimalScale(const CCVector3d &optimalScale);
    /// Set the scale that was used in the file the point cloud
    /// to save comes from.
    void setSavedScale(const CCVector3d &savedScale);
    /// Set the extra LAS scalar fields saved from the original file.
    void setExtraScalarFields(const std::vector<LasExtraScalarField> &extraScalarFields);

    /// Returns the point format currently selected
    unsigned int selectedPointFormat() const;
    /// Returns the minor version currently selected
    unsigned int selectedVersionMinor() const;
    /// Returns the currently selected scale
    CCVector3d chosenScale() const;
    /// Returns whether the user wants to save RGB
    bool shouldSaveRGB() const;
    /// Returns whether the user wants to save the Waveforms
    bool shouldSaveWaveform() const;
    /// Returns the vector of LAS scalar fields the user wants to save.
    /// 
    /// Each LAS scalar fields is mapped to an existing point cloud's ccScalarField.
    /// The mapping is done by us and the user.
    std::vector<LasScalarField> fieldsToSave() const;

  public Q_SLOTS:
    void handleSelectedVersionChange(const QString &);
    void handleSelectedPointFormatChange(int index);
    void handleComboBoxChange(int index);

  private:
    ccPointCloud *m_cloud{nullptr};
    QStringListModel *m_comboBoxModel{nullptr};
    /// Contains the mapping from a LAS field name to a combo box
    /// where the user (or us) selected the scalar field to use
    std::vector<std::pair<MappingLabel *, QComboBox *>> m_scalarFieldMapping;
};
