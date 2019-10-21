#include "stockerDatabase.h"
#include "ioctrl/StFileOperator.hpp"

#include "ccHObject.h"
#include "ccHObjectCaster.h"
#include "ccDBRoot.h"

#include "mainwindow.h"
#include "QFileInfo"
#include <QImageReader>
#include <QFileDialog>
#include "FileIOFilter.h"
#include <QDir>
#include <QStringLiteral>

#include "BlockDBaseIO.h"

using namespace stocker;

DataBaseHObject * GetRootDataBase(StHObject * obj)
{
	StHObject* bd_obj_ = obj;
	do {
		if (isDatabaseProject(bd_obj_)) {
			return static_cast<DataBaseHObject*>(bd_obj_);
		}
		bd_obj_ = bd_obj_->getParent();
	} while (bd_obj_);

	return nullptr;
}

BDBaseHObject* GetRootBDBase(StHObject* obj) {
	StHObject* bd_obj_ = obj;
	do {
		if (isBuildingProject(bd_obj_)) {
			return static_cast<BDBaseHObject*>(bd_obj_);
		}
		bd_obj_ = bd_obj_->getParent();
	} while (bd_obj_);

	return nullptr;
}

BDImageBaseHObject* GetRootImageBase(StHObject* obj) {
	StHObject* bd_obj_ = obj;
	do {
		if (isImageProject(bd_obj_)) {
			return static_cast<BDImageBaseHObject*>(bd_obj_);
		}
		bd_obj_ = bd_obj_->getParent();
	} while (bd_obj_);

	return nullptr;
}

StHObject* getChildGroupByName(StHObject* group, QString name, bool auto_create, bool add_to_db, bool keep_dir_hier)
{
	StHObject* find_obj = nullptr;
	for (size_t i = 0; i < group->getChildrenNumber(); i++) {
		StHObject* child = group->getChild(i);
		if (child->isGroup() && child->getName() == name) {
			find_obj = child;
		}
	}
	if (!find_obj && auto_create) {
		find_obj = new StHObject(name);
		if (keep_dir_hier) {
			QString path = group->getPath() + "/" + name;
			if (StCreatDir(path)) {
				find_obj->setPath(path);
			}
			else {
				delete find_obj; find_obj = nullptr;
				return nullptr;
			}
		}

		group->addChild(find_obj);
		if (add_to_db) {
			MainWindow::TheInstance()->addToDB(find_obj, group->getDBSourceType());
		}
	}
	else if (find_obj) {
		QString path = find_obj->getPath();
		if (path.isEmpty() && QFileInfo(path).isDir()) {
			StCreatDir(path);
		}
	}
	return find_obj;
}

inline void DataBaseHObject::setPath(const QString & tp)
{
	m_path = tp;
	QString prj_path = getPath() + "/" + QFileInfo(getPath()).completeBaseName() + ".bprj";
	if (m_blkData) {
		m_blkData->setPath(prj_path.toLocal8Bit());
	}
}

StHObject * DataBaseHObject::getPointCloudGroup()
{
	return getChildGroupByName(this, PtCld_Dir_NAME);
}

StHObject * DataBaseHObject::getImagesGroup()
{
	return getChildGroupByName(this, IMAGE_Dir_NAME);
}

StHObject * DataBaseHObject::getMiscsGroup()
{
	return getChildGroupByName(this, MISCS_Dir_NAME);
}

StHObject* DataBaseHObject::getProductGroup() {
	return getChildGroupByName(this, PRODS_Dir_NAME);
}

StHObject * DataBaseHObject::getProductItem(QString name)
{
	StHObject* products = getProductGroup();
	if (!products) { return nullptr; }
	return getChildGroupByName(products, name, true, false, true);
}
StHObject* DataBaseHObject::getProductFiltered() 
{
	return getProductItem("filtered");
}
StHObject* DataBaseHObject::getProductClassified() 
{
	return getProductItem("classified");
}
StHObject * DataBaseHObject::getProductSegmented()
{
	return getProductItem("segmented");
}
StHObject * DataBaseHObject::getProductModels()
{
	return getProductItem("models");
}

StHObject* createObjectFromBlkDataInfo(BlockDB::blkDataInfo* info)
{
	StHObject* object = nullptr;
	if (!info || !info->isValid()) return object;
	switch (info->dataType())
	{
	case BlockDB::Blk_PtCld: {
		//! fast load
		FileIOFilter::LoadParameters parameters;
		CC_FILE_ERROR result = CC_FERR_NO_ERROR;
		{
			parameters.alwaysDisplayLoadDialog = false;
			parameters.loadMode = 0;
		}
		object = FileIOFilter::LoadFromFile(QString::fromLocal8Bit(info->sPath), parameters, result, QString());
	}
		break;
	case BlockDB::Blk_Image:
		object = new StHObject(QString::fromLocal8Bit(info->sName));
		object->setPath(QString::fromLocal8Bit(info->sPath));
		break;
	case BlockDB::Blk_Camera:
		object = new StHObject(QString::fromLocal8Bit(info->sName));
		object->setPath(QString::fromLocal8Bit(info->sPath));
		break;
	case BlockDB::Blk_Miscs:
		object = new StHObject(QString::fromLocal8Bit(info->sName));
		object->setPath(QString::fromLocal8Bit(info->sPath));
		break;
	default:
		break;
	}
	return object;
}

DataBaseHObject * DataBaseHObject::Create(QString absolute_path)
{
	DataBaseHObject* new_database = new DataBaseHObject(QFileInfo(absolute_path).completeBaseName());
	new_database->setPath(QFileInfo(absolute_path).absoluteFilePath());

	//! point clouds
	{
		StHObject* points = new StHObject(PtCld_Dir_NAME);
		new_database->addChild(points);
		points->setLocked(true);
	}

	//! images
	{
		StHObject* images = new StHObject(IMAGE_Dir_NAME);
		if (!images) {
			return nullptr;
		}
		new_database->addChild(images);
		images->setLocked(true);
	}

	//! miscellaneous
	{
		StHObject* misc = new StHObject(MISCS_Dir_NAME);
		if (!misc) {
			return nullptr;
		}
		new_database->addChild(misc);
		misc->setLocked(true);
	}

	//! products
	{
		StHObject* products = new StHObject(PRODS_Dir_NAME);
		if (!products) {
			return nullptr;
		}
		new_database->addChild(products);
		products->setLocked(true);

		StHObject* groundFilter = new_database->getProductFiltered();
		if (!groundFilter) {
			return nullptr;
		}
		groundFilter->setLocked(true);

		StHObject* classified = new_database->getProductClassified();
		if (!classified) {
			return nullptr;
		}
		classified->setLocked(true);

		StHObject* segments = new_database->getProductSegmented();
		if (!segments) {
			return nullptr;
		}
		segments->setLocked(true);

		StHObject* models = new_database->getProductModels();
		if (!models) {
			return nullptr;
		}
		models->setLocked(true);
	}
	return new_database;
}

bool DataBaseHObject::addData(StHObject * obj, BlockDB::blkDataInfo* info, bool exist_info)
{
	if (!info) {
		return false;
	}
	if (info->dataType() == BlockDB::Blk_unset) {
		return false;
	}
	if (m_blkData) {
		BlockDB::blkDataInfo* added_info = !exist_info ? m_blkData->addData(info) : info;
		if (added_info)	{
			obj->setMetaData(BLK_DATA_METAKEY, QVariant::fromValue(added_info));

// 			if (obj->hasMetaData("BlkDataInfo")) {
// 				QVariant v = obj->getMetaData("BlkDataInfo");
// 				if (v.canConvert<BlockDB::blkDataInfo*>()) {
// 					BlockDB::blkPtCldInfo* pin = static_cast<BlockDB::blkPtCldInfo*>(v.value<BlockDB::blkDataInfo*>());
// 
// 					if (pin == &m_blkData->getPtClds().back()) {
// 						auto test = pin->scene_info;
// 					}
// 				}
// 			}
		}
	}
	StHObject* importObj = nullptr;
	switch (info->dataType())
	{
	case BlockDB::Blk_PtCld: {
		BlockDB::blkPtCldInfo* pInfo = static_cast<BlockDB::blkPtCldInfo*>(info);
		BlockDB::BLOCK_PtCldLevel level = pInfo->level;
		if (level >= BlockDB::PCLEVEL_STRIP && level <= BlockDB::PCLEVEL_TILE) {
			importObj = getPointCloudGroup();
		}
		else if (level == BlockDB::PCLEVEL_FILTER) {
			importObj = getProductFiltered();
		}
		else if (level == BlockDB::PCLEVEL_CLASS) {
			importObj = getProductClassified();
		}
		else if (level == BlockDB::PCLEVEL_BUILD) {
			importObj = getProductSegmented();
		}
	}
		break;
	case BlockDB::Blk_Image:
		importObj = getImagesGroup();
		break;
	case BlockDB::Blk_Camera:
		importObj = getMiscsGroup();
		break;
	case BlockDB::Blk_Miscs:
		importObj = getMiscsGroup();
		break;
	default:
		break;
	}
	return importObj && importObj->addChild(obj);
}

bool DataBaseHObject::addDataExist(BlockDB::blkDataInfo * info)
{
	StHObject* obj = createObjectFromBlkDataInfo(info);
	if (!obj) { return false; }
	
	return addData(obj, info, true);
}

void DataBaseHObject::clear()
{
	if (m_blkData) {
		m_blkData->clear();
	}
	StHObject* group = getImagesGroup(); if (group)group->removeAllChildren();
	group = getPointCloudGroup(); if (group)group->removeAllChildren();
	group = getMiscsGroup(); if (group)group->removeAllChildren();
	group = getProductFiltered(); if (group)group->removeAllChildren();
	group = getProductClassified(); if (group)group->removeAllChildren();
	group = getProductSegmented(); if (group)group->removeAllChildren();
	group = getProductModels(); if (group)group->removeAllChildren();
}

bool DataBaseHObject::load()
{
	if (!m_blkData)	{
		m_blkData = new BlockDB::BlockDBaseIO;
		setPath(getPath());
	}
	clear();

	try	{
		std::cout << "loading project" << getPath().toStdString() << std::endl;
		if (!m_blkData->loadProject()) {
			std::cout << "cannot load project: " + m_blkData->getErrorInfo() << std::endl;
			return false;
		}
		std::cout << "project loaded" << std::endl;
	}
	catch (const std::exception& e) {
		STOCKER_ERROR_ASSERT(e.what());
		return false;
	}

	try {
		StHObject* group = getPointCloudGroup();
		if (group) {
			group->setPath(QFileInfo(m_blkData->projHdr().lasListPath).absolutePath());
		}
		else return false;

		for (auto & info : m_blkData->getPtClds()) {
			addDataExist(&info);
		}
		std::cout << QString::number(group->getChildrenNumber()).toStdString() << " point clouds added" << std::endl;

		group = getImagesGroup();
		if (group) {
			group->setPath(QFileInfo(m_blkData->projHdr().imgListPath).absolutePath());
		}
		else return false;

		for (auto & info : m_blkData->getImages()) {
			addDataExist(&info);
		}
		std::cout << QString::number(group->getChildrenNumber()).toStdString() << " images added" << std::endl;

		int cam_count(0);
		for (auto & info : m_blkData->getCameras()) {
			if (addDataExist(&info))
				cam_count++;
		}
		std::cout << cam_count << " cameras added" << std::endl;

		group = getMiscsGroup();
		if (group) {
			group->setPath(QFileInfo(m_blkData->projHdr().m_strProdGCDPN).absolutePath());
		}
		else return false;
	}
	catch (const std::exception& e) {
		STOCKER_ERROR_ASSERT(e.what());
		return false;
	}

	return true;
}

bool DataBaseHObject::save()
{	
	try	{
		if (!m_blkData->saveProject()) {
			std::cout << m_blkData->getErrorInfo() << std::endl;
			return false;
		}
	}
	catch (const std::exception& e) {
		STOCKER_ERROR_ASSERT(e.what());
		return false;
	}
	
	return true;
}

StBuilding* BDBaseHObject::GetBuildingGroup(QString building_name, bool check_enable) {
	StHObject* obj = GetHObj(CC_TYPES::ST_BUILDING, "", building_name, check_enable);
	if (obj) return static_cast<StBuilding*>(obj);
	return nullptr;
}
ccPointCloud * BDBaseHObject::GetOriginPointCloud(QString building_name, bool check_enable) {
	StHObject* obj = GetHObj(CC_TYPES::POINT_CLOUD, BDDB_ORIGIN_CLOUD_SUFFIX, building_name, check_enable);
	if (obj) return static_cast<ccPointCloud*>(obj);
	return nullptr;
}
StPrimGroup * BDBaseHObject::GetPrimitiveGroup(QString building_name) {
	StHObject* obj = GetHObj(CC_TYPES::ST_PRIMGROUP, BDDB_PRIMITIVE_SUFFIX, building_name, false);
	if (obj) return static_cast<StPrimGroup*>(obj);
	StPrimGroup* group = new StPrimGroup(building_name + BDDB_PRIMITIVE_SUFFIX);
	if (group) {
		StHObject* bd = GetBuildingGroup(building_name, false);
		if (bd) { bd->addChild(group); MainWindow::TheInstance()->addToDB(group, this->getDBSourceType()); return group; }
		else { delete group; group = nullptr; }
	}
	return nullptr;
}
StBlockGroup * BDBaseHObject::GetBlockGroup(QString building_name) {
	StHObject* obj = GetHObj(CC_TYPES::ST_BLOCKGROUP, BDDB_BLOCKGROUP_SUFFIX, building_name, false);
	if (obj) return static_cast<StBlockGroup*>(obj);
	StBlockGroup* group = new StBlockGroup(building_name + BDDB_BLOCKGROUP_SUFFIX);
	if (group) {
		StHObject* bd = GetBuildingGroup(building_name, false);
		if (bd) { bd->addChild(group); MainWindow::TheInstance()->addToDB(group, this->getDBSourceType()); return group; }
		else { delete group; group = nullptr; }
	}
	return nullptr;
}
StPrimGroup * BDBaseHObject::GetHypothesisGroup(QString building_name) {
	StHObject* obj = GetHObj(CC_TYPES::ST_PRIMGROUP, BDDB_POLYFITHYPO_SUFFIX, building_name, false);
	if (obj) return static_cast<StPrimGroup*>(obj);
	StPrimGroup* group = new StPrimGroup(building_name + BDDB_POLYFITHYPO_SUFFIX);
	if (group) {
		StHObject* bd = GetBuildingGroup(building_name, false);
		if (bd) { bd->addChild(group); MainWindow::TheInstance()->addToDB(group, this->getDBSourceType()); return group; }
		else { delete group; group = nullptr; }
	}
	return nullptr;
}
StHObject * BDBaseHObject::GetTodoGroup(QString building_name)
{
	StHObject* obj = GetHObj(CC_TYPES::HIERARCHY_OBJECT, BDDB_TODOGROUP_SUFFIX, building_name, false);
	if (obj) return static_cast<StBlockGroup*>(obj);
	StBlockGroup* group = new StBlockGroup(building_name + BDDB_TODOGROUP_SUFFIX);
	if (group) {
		group->setDisplay(getDisplay());
		StHObject* bd = GetBuildingGroup(building_name, false);
		if (bd) { bd->addChild(group); MainWindow::TheInstance()->addToDB(group, this->getDBSourceType()); return group; }
		else { delete group; group = nullptr; }
	}
	return nullptr;
}
ccPointCloud * BDBaseHObject::GetTodoPoint(QString buildig_name)
{
	StHObject* todo_group = GetTodoGroup(buildig_name);
	if (!todo_group) { throw std::runtime_error("internal error"); return nullptr; }
	StHObject::Container todo_children;
	todo_group->filterChildrenByName(todo_children, false, BDDB_TODOPOINT_PREFIX, true, CC_TYPES::POINT_CLOUD);
	if (!todo_children.empty()) {
		return ccHObjectCaster::ToPointCloud(todo_children.front());
	}
	else {
		ccPointCloud* todo_point = new ccPointCloud(BDDB_TODOPOINT_PREFIX);
		todo_point->setGlobalScale(global_scale);
		todo_point->setGlobalShift(CCVector3d(vcgXYZ(global_shift)));
		todo_point->setDisplay(todo_group->getDisplay());
		todo_point->showColors(true);
		todo_group->addChild(todo_point);
		MainWindow* win = MainWindow::TheInstance();
		assert(win);
		win->addToDB(todo_point, this->getDBSourceType(), false, false);
		return todo_point;
	}
	return nullptr;
}
ccPointCloud * BDBaseHObject::GetTodoLine(QString buildig_name)
{
	StHObject* todo_group = GetTodoGroup(buildig_name);
	if (!todo_group) { throw std::runtime_error("internal error"); return nullptr; }
	StHObject::Container todo_children;
	todo_group->filterChildrenByName(todo_children, false, BDDB_TODOLINE_PREFIX, true, CC_TYPES::POINT_CLOUD);
	if (!todo_children.empty()) {
		return ccHObjectCaster::ToPointCloud(todo_children.front());
	}
	else {
		ccPointCloud* todo_point = new ccPointCloud(BDDB_TODOLINE_PREFIX);
		todo_point->setGlobalScale(global_scale);
		todo_point->setGlobalShift(CCVector3d(vcgXYZ(global_shift)));
		todo_point->setDisplay(getDisplay());
		todo_group->addChild(todo_point);
		MainWindow* win = MainWindow::TheInstance();
		assert(win);
		win->addToDB(todo_point, this->getDBSourceType(), false, false);
		return todo_point;
	}
	return nullptr;
}
std::string BDBaseHObject::GetPathModelObj(std::string building_name)
{
	auto& bd = block_prj.m_builder.sbuild.find(stocker::BuilderBase::BuildNode::Create(building_name));
	if (bd == block_prj.m_builder.sbuild.end()) {
		throw std::runtime_error("cannot find building!" + building_name);
	}
	return std::string((*bd)->data.file_path.model_dir + building_name + MODEL_LOD3_OBJ_SUFFIX);
}

const stocker::BuildUnit BDBaseHObject::GetBuildingUnit(std::string building_name) {
	auto iter = block_prj.m_builder.sbuild.find(BuilderBase::BuildNode::Create(building_name));
	if (iter == block_prj.m_builder.sbuild.end()) {
		throw runtime_error("internal error: cannot find building");
		return stocker::BuildUnit("invalid");
	}
	else return (*iter)->data;
}

stocker::BuilderBase::SpBuild BDBaseHObject::GetBuildingSp(std::string building_name)
{
	auto iter = block_prj.m_builder.sbuild.find(BuilderBase::BuildNode::Create(building_name));
	if (iter == block_prj.m_builder.sbuild.end())
		return nullptr;
	else
		return *iter;
	//return stocker::BuilderSet::SpBuild();
}

StHObject* findChildByName(StHObject* parent,
	bool recursive,
	QString filter,
	bool strict,
	CC_CLASS_ENUM type_filter/*=CC_TYPES::OBJECT*/,
	bool auto_create /*= false*/,
	ccGenericGLDisplay* inDisplay/*=0*/)
{
	StHObject::Container children;
	parent->filterChildrenByName(children, recursive, filter, strict, type_filter, inDisplay);

	if (children.empty()) {
		if (auto_create) {
			StHObject* obj = StHObject::New(type_filter, filter.toStdString().c_str());
			if (parent->getDisplay()) {
				obj->setDisplay(parent->getDisplay());
			}
			parent->addChild(obj);
			MainWindow::TheInstance()->addToDB(obj, parent->getDBSourceType(), false, false);
			return obj;
		}
		else
			return nullptr;
	}
	else {
		return children.front();
	}
}

int GetNumberExcludePrefix(StHObject * obj, QString prefix)
{
	QString name = obj->getName();
	if (name.startsWith(prefix) && name.length() > prefix.length()) {
		QString number = name.mid(prefix.length(), name.length());
		return number.toInt();
	}
	return -1;
}

int GetMaxNumberExcludeChildPrefix(StHObject * obj, QString prefix/*, CC_CLASS_ENUM type = CC_TYPES::OBJECT*/)
{
	if (!obj) { return -1; }
	set<int> name_numbers;
	for (size_t i = 0; i < obj->getChildrenNumber(); i++) {
		QString name = obj->getChild(i)->getName();
		int number = GetNumberExcludePrefix(obj->getChild(i), prefix);
		if (number >= 0) {
			name_numbers.insert(number);
		}
	}
	if (!name_numbers.empty()) {
		return *name_numbers.rbegin();
	}
	return -1;
}

bool StCreatDir(QString dir)
{
	if (QDir(dir).exists()) {
		return true;
	}
	return CreateDir(dir.toStdString().c_str());
}

QStringList moveFilesToDir(QStringList list, QString dir)
{
	QStringList new_files;
	if (StCreatDir(dir)) {
		for (size_t i = 0; i < list.size(); i++) {
			QString & file = const_cast<QString&>(list[i]);
			QFileInfo file_info(file);
			QString new_file = dir + "/" + file_info.fileName();
			if (QFile::copy(file, new_file)) {
				QFile::remove(file);
				new_files.append(new_file);
			}
			else {
				new_files.append(file);
			}
		}
	}
	else {
		return list;
	}

	return new_files;
}

QStringList copyFilesToDir(QStringList list, QString dir)
{
	QStringList new_files;
	if (StCreatDir(dir)) {
		for (size_t i = 0; i < list.size(); i++) {
			QString & file = const_cast<QString&>(list[i]);
			QFileInfo file_info(file);
			QString new_file = dir + "/" + file_info.fileName();
			if (QFile::copy(file, new_file)) {
				new_files.append(new_file);
			}
			else {
				new_files.append(file);
			}
		}
	}
	else {
		return list;
	}

	return new_files;
}
