// ##########################################################################
// #                                                                        #
// #                              CLOUDCOMPARE                              #
// #                                                                        #
// #  This program is free software; you can redistribute it and/or modify  #
// #  it under the terms of the GNU General Public License as published by  #
// #  the Free Software Foundation; version 2 or later of the License.      #
// #                                                                        #
// #  This program is distributed in the hope that it will be useful,       #
// #  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
// #  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
// #  GNU General Public License for more details.                          #
// #                                                                        #
// #          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
// #                                                                        #
// ##########################################################################

// GUIs generated by Qt Designer
#include <ui_saveAsciiFileDlg.h>

// local
#include "AsciiSaveDlg.h"

// Qt
#include <QComboBox>
#include <QSettings>
#include <QSpinBox>

// system
#include <assert.h>

AsciiSaveDlg::AsciiSaveDlg(QWidget* parent)
    : QDialog(parent)
    , m_ui(new Ui_AsciiSaveDialog)
{
	m_ui->setupUi(this);

	connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, &AsciiSaveDlg::acceptAndSaveSettings);

	initFromPersistentSettings();
}

AsciiSaveDlg::~AsciiSaveDlg()
{
	if (m_ui)
		delete m_ui;
}

bool AsciiSaveDlg::saveColumnsNamesHeader() const
{
	return m_ui->columnsHeaderCheckBox->isChecked();
}

void AsciiSaveDlg::enableSaveColumnsNamesHeader(bool state)
{
	m_ui->columnsHeaderCheckBox->setChecked(state);
}

bool AsciiSaveDlg::savePointCountHeader() const
{
	return m_ui->pointCountHeaderCheckBox->isChecked();
}

void AsciiSaveDlg::enableSavePointCountHeader(bool state)
{
	m_ui->pointCountHeaderCheckBox->setChecked(state);
}

void AsciiSaveDlg::setSaveFloatColors(bool state)
{
	m_ui->saveFloatColorsCheckBox->setChecked(state);
}

bool AsciiSaveDlg::saveFloatColors() const
{
	return m_ui->saveFloatColorsCheckBox->isChecked();
}

void AsciiSaveDlg::setSaveAlphaChannel(bool state)
{
	m_ui->saveAlphaChannelCheckBox->setChecked(state);
}

bool AsciiSaveDlg::saveAlphaChannel() const
{
	return m_ui->saveAlphaChannelCheckBox->isChecked();
}

unsigned char AsciiSaveDlg::getSeparator() const
{
	switch (m_ui->separatorComboBox->currentIndex())
	{
	case 0:
		return ' ';
	case 1:
		return ';';
	case 2:
		return ',';
	case 3:
		return '\t';
	default:
		assert(false);
	}

	return 0;
}

void AsciiSaveDlg::setSeparatorIndex(int index)
{
	m_ui->separatorComboBox->setCurrentIndex(index);
}

int AsciiSaveDlg::getSeparatorIndex() const
{
	return m_ui->separatorComboBox->currentIndex();
}

int AsciiSaveDlg::coordsPrecision() const
{
	return m_ui->coordsPrecisionSpinBox->value();
}

void AsciiSaveDlg::setCoordsPrecision(int prec)
{
	m_ui->coordsPrecisionSpinBox->setValue(prec);
}

int AsciiSaveDlg::sfPrecision() const
{
	return m_ui->sfPrecisionSpinBox->value();
}

void AsciiSaveDlg::setSfPrecision(int prec)
{
	m_ui->sfPrecisionSpinBox->setValue(prec);
}

bool AsciiSaveDlg::swapColorAndSF() const
{
	return m_ui->orderComboBox->currentIndex() == 1;
}

void AsciiSaveDlg::enableSwapColorAndSF(bool state)
{
	m_ui->orderComboBox->setCurrentIndex(state ? 1 : 0);
}

void AsciiSaveDlg::initFromPersistentSettings()
{
	QSettings settings;
	settings.beginGroup("AsciiSaveDialog");

	// read parameters
	bool saveColHeader    = settings.value("saveHeader", m_ui->columnsHeaderCheckBox->isChecked()).toBool();
	bool savePtsHeader    = settings.value("savePtsHeader", m_ui->pointCountHeaderCheckBox->isChecked()).toBool();
	int  coordsPrecision  = settings.value("coordsPrecision", m_ui->coordsPrecisionSpinBox->value()).toInt();
	int  sfPrecision      = settings.value("sfPrecision", m_ui->sfPrecisionSpinBox->value()).toInt();
	int  separatorIndex   = settings.value("separator", m_ui->separatorComboBox->currentIndex()).toInt();
	int  orderIndex       = settings.value("saveOrder", m_ui->orderComboBox->currentIndex()).toInt();
	bool saveFloatColors  = settings.value("saveFloatColors", m_ui->saveFloatColorsCheckBox->isChecked()).toBool();
	bool saveAlphaChannel = settings.value("saveAlphaChannel", m_ui->saveAlphaChannelCheckBox->isChecked()).toBool();

	// apply parameters
	m_ui->columnsHeaderCheckBox->setChecked(saveColHeader);
	m_ui->pointCountHeaderCheckBox->setChecked(savePtsHeader);
	m_ui->coordsPrecisionSpinBox->setValue(coordsPrecision);
	m_ui->sfPrecisionSpinBox->setValue(sfPrecision);
	m_ui->separatorComboBox->setCurrentIndex(separatorIndex);
	m_ui->orderComboBox->setCurrentIndex(orderIndex);
	m_ui->saveFloatColorsCheckBox->setChecked(saveFloatColors);
	m_ui->saveAlphaChannelCheckBox->setChecked(saveAlphaChannel);

	settings.endGroup();
}

void AsciiSaveDlg::acceptAndSaveSettings()
{
	QSettings settings;
	settings.beginGroup("AsciiSaveDialog");

	// write parameters
	settings.setValue("saveHeader", m_ui->columnsHeaderCheckBox->isChecked());
	settings.setValue("savePtsHeader", m_ui->pointCountHeaderCheckBox->isChecked());
	settings.setValue("coordsPrecision", m_ui->coordsPrecisionSpinBox->value());
	settings.setValue("sfPrecision", m_ui->sfPrecisionSpinBox->value());
	settings.setValue("separator", m_ui->separatorComboBox->currentIndex());
	settings.setValue("saveOrder", m_ui->orderComboBox->currentIndex());
	settings.setValue("saveFloatColors", m_ui->saveFloatColorsCheckBox->isChecked());
	settings.setValue("saveAlphaChannel", m_ui->saveAlphaChannelCheckBox->isChecked());

	settings.endGroup();
}
