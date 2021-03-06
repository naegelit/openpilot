/**
 ******************************************************************************
 *
 * @file       uavobjecttreemodel.cpp
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @addtogroup GCSPlugins GCS Plugins
 * @{
 * @addtogroup UAVObjectBrowserPlugin UAVObject Browser Plugin
 * @{
 * @brief The UAVObject Browser gadget plugin
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 3 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License 
 * for more details.
 * 
 * You should have received a copy of the GNU General Public License along 
 * with this program; if not, write to the Free Software Foundation, Inc., 
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "uavobjecttreemodel.h"
#include "fieldtreeitem.h"
#include "uavobjectmanager.h"
#include "uavdataobject.h"
#include "uavmetaobject.h"
#include "uavobjectfield.h"
#include "extensionsystem/pluginmanager.h"
#include <QtGui/QColor>
//#include <QtGui/QIcon>
#include <QtCore/QTimer>
#include <QtCore/QSignalMapper>
#include <QtCore/QDebug>

UAVObjectTreeModel::UAVObjectTreeModel(QObject *parent) :
        QAbstractItemModel(parent),
        m_recentlyUpdatedTimeout(500), // ms
        m_recentlyUpdatedColor(QColor(255, 230, 230)),
        m_manuallyChangedColor(QColor(230, 230, 255))
{
    ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
    UAVObjectManager *objManager = pm->getObject<UAVObjectManager>();

    connect(objManager, SIGNAL(newObject(UAVObject*)), this, SLOT(newObject(UAVObject*)));
    connect(objManager, SIGNAL(newInstance(UAVObject*)), this, SLOT(newObject(UAVObject*)));

    TreeItem::setHighlightTime(m_recentlyUpdatedTimeout);
    setupModelData(objManager);
}

UAVObjectTreeModel::~UAVObjectTreeModel()
{
    delete m_rootItem;
}

void UAVObjectTreeModel::setupModelData(UAVObjectManager *objManager)
{
    // root
    QList<QVariant> rootData;
    rootData << tr("Property") << tr("Value") << tr("Unit");
    m_rootItem = new TreeItem(rootData);

    m_settingsTree = new TopTreeItem(tr("Settings"), m_rootItem);
    m_rootItem->appendChild(m_settingsTree);
    m_nonSettingsTree = new TopTreeItem(tr("Data Objects"), m_rootItem);
    m_rootItem->appendChild(m_nonSettingsTree);
    connect(m_settingsTree, SIGNAL(updateHighlight(TreeItem*)), this, SLOT(updateHighlight(TreeItem*)));
    connect(m_nonSettingsTree, SIGNAL(updateHighlight(TreeItem*)), this, SLOT(updateHighlight(TreeItem*)));

    QList< QList<UAVDataObject*> > objList = objManager->getDataObjects();
    foreach (QList<UAVDataObject*> list, objList) {
        foreach (UAVDataObject* obj, list) {
            addDataObject(obj);
        }
    }
}

void UAVObjectTreeModel::newObject(UAVObject *obj)
{
    UAVDataObject *dobj = qobject_cast<UAVDataObject*>(obj);
    if (dobj)
        addDataObject(dobj);
}

void UAVObjectTreeModel::addDataObject(UAVDataObject *obj)
{
    TopTreeItem *root = obj->isSettings() ? m_settingsTree : m_nonSettingsTree;
    if (root->objIds().contains(obj->getObjID())) {
        int index = root->objIds().indexOf(obj->getObjID());
        addInstance(obj, root->child(index));
    } else {
        DataObjectTreeItem *data = new DataObjectTreeItem(obj->getName());
        connect(data, SIGNAL(updateHighlight(TreeItem*)), this, SLOT(updateHighlight(TreeItem*)));
        int index = root->nameIndex(obj->getName());
        root->insert(index, data);
        root->insertObjId(index, obj->getObjID());
        UAVMetaObject *meta = obj->getMetaObject();
        addMetaObject(meta, data);
        addInstance(obj, data);
    }
}

void UAVObjectTreeModel::addMetaObject(UAVMetaObject *obj, TreeItem *parent)
{
    connect(obj, SIGNAL(objectUpdated(UAVObject*)), this, SLOT(highlightUpdatedObject(UAVObject*)));
    MetaObjectTreeItem *meta = new MetaObjectTreeItem(obj, tr("Meta Data"));
    connect(meta, SIGNAL(updateHighlight(TreeItem*)), this, SLOT(updateHighlight(TreeItem*)));
    foreach (UAVObjectField *field, obj->getFields()) {
        if (field->getNumElements() > 1) {
            addArrayField(field, meta);
        } else {
            addSingleField(0, field, meta);
        }
    }
    parent->appendChild(meta);
}

void UAVObjectTreeModel::addInstance(UAVObject *obj, TreeItem *parent)
{
    connect(obj, SIGNAL(objectUpdated(UAVObject*)), this, SLOT(highlightUpdatedObject(UAVObject*)));
    TreeItem *item;
    if (obj->isSingleInstance()) {
        item = parent;
        DataObjectTreeItem *p = static_cast<DataObjectTreeItem*>(parent);
        p->setObject(obj);
    } else {
        QString name = tr("Instance") +  " " + QString::number(obj->getInstID());
        item = new InstanceTreeItem(obj, name);
        connect(item, SIGNAL(updateHighlight(TreeItem*)), this, SLOT(updateHighlight(TreeItem*)));
        parent->appendChild(item);
    }
    foreach (UAVObjectField *field, obj->getFields()) {
        if (field->getNumElements() > 1) {
            addArrayField(field, item);
        } else {
            addSingleField(0, field, item);
        }
    }
}


void UAVObjectTreeModel::addArrayField(UAVObjectField *field, TreeItem *parent)
{
    TreeItem *item = new ArrayFieldTreeItem(field->getName());
    connect(item, SIGNAL(updateHighlight(TreeItem*)), this, SLOT(updateHighlight(TreeItem*)));
    for (uint i = 0; i < field->getNumElements(); ++i) {
        addSingleField(i, field, item);
    }
    parent->appendChild(item);
}

void UAVObjectTreeModel::addSingleField(int index, UAVObjectField *field, TreeItem *parent)
{
    QList<QVariant> data;
    if (field->getNumElements() == 1)
        data.append(field->getName());
    else
        data.append( QString("[%1]").arg((field->getElementNames())[index]) );

    FieldTreeItem *item;
    UAVObjectField::FieldType type = field->getType();
    switch (type) {
    case UAVObjectField::ENUM: {
        QStringList options = field->getOptions();
        QVariant value = field->getValue();
        data.append( options.indexOf(value.toString()) );
        data.append(field->getUnits());
        item = new EnumFieldTreeItem(field, index, data);
        break;
    }
    case UAVObjectField::INT8:
    case UAVObjectField::INT16:
    case UAVObjectField::INT32:
    case UAVObjectField::UINT8:
    case UAVObjectField::UINT16:
    case UAVObjectField::UINT32:
        data.append(field->getValue(index));
        data.append(field->getUnits());
        item = new IntFieldTreeItem(field, index, data);
        break;
    case UAVObjectField::FLOAT32:
        data.append(field->getValue(index));
        data.append(field->getUnits());
        item = new FloatFieldTreeItem(field, index, data);
        break;
    default:
        Q_ASSERT(false);
    }
    connect(item, SIGNAL(updateHighlight(TreeItem*)), this, SLOT(updateHighlight(TreeItem*)));
    parent->appendChild(item);
}

QModelIndex UAVObjectTreeModel::index(int row, int column, const QModelIndex &parent)
        const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    TreeItem *parentItem;

    if (!parent.isValid())
        parentItem = m_rootItem;
    else
        parentItem = static_cast<TreeItem*>(parent.internalPointer());

    TreeItem *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    else
        return QModelIndex();
}

QModelIndex UAVObjectTreeModel::index(TreeItem *item)
{
    if (item->parent() == 0)
        return QModelIndex();

    QModelIndex root = index(item->parent());

    for (int i = 0; i < rowCount(root); ++i) {
        QModelIndex childIndex = index(i, 0, root);
        TreeItem *child = static_cast<TreeItem*>(childIndex.internalPointer());
        if (child == item)
            return childIndex;
    }
    Q_ASSERT(false);
    return QModelIndex();
}

QModelIndex UAVObjectTreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    TreeItem *childItem = static_cast<TreeItem*>(index.internalPointer());
    TreeItem *parentItem = childItem->parent();

    if (parentItem == m_rootItem)
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int UAVObjectTreeModel::rowCount(const QModelIndex &parent) const
{
    TreeItem *parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = m_rootItem;
    else
        parentItem = static_cast<TreeItem*>(parent.internalPointer());

    return parentItem->childCount();
}

int UAVObjectTreeModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return static_cast<TreeItem*>(parent.internalPointer())->columnCount();
    else
        return m_rootItem->columnCount();
}

QVariant UAVObjectTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (index.column() == TreeItem::dataColumn && role == Qt::EditRole) {
        TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
        return item->data(index.column());
    }
//    if (role == Qt::DecorationRole)
//        return QIcon(":/core/images/openpilot_logo_128.png");

    if (role == Qt::ToolTipRole) {
        TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
        return item->description();
    }

    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());

    if (index.column() == 0 && role == Qt::BackgroundRole) {
        ObjectTreeItem *objItem = dynamic_cast<ObjectTreeItem*>(item);
        if (objItem && objItem->highlighted())
            return QVariant(m_recentlyUpdatedColor);
    }
    if (index.column() == TreeItem::dataColumn && role == Qt::BackgroundRole) {
        FieldTreeItem *fieldItem = dynamic_cast<FieldTreeItem*>(item);
        if (fieldItem && fieldItem->highlighted())
            return QVariant(m_recentlyUpdatedColor);
        if (fieldItem && fieldItem->changed())
            return QVariant(m_manuallyChangedColor);
    }

    if (role != Qt::DisplayRole)
        return QVariant();

    if (index.column() == TreeItem::dataColumn) {
        EnumFieldTreeItem *fieldItem = dynamic_cast<EnumFieldTreeItem*>(item);
        if (fieldItem) {
            int enumIndex = fieldItem->data(index.column()).toInt();
            return fieldItem->enumOptions(enumIndex);
        }
    }

    return item->data(index.column());
}

bool UAVObjectTreeModel::setData(const QModelIndex &index, const QVariant & value, int role)
{
    Q_UNUSED(role)
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
    item->setData(value, index.column());
    return true;
}


Qt::ItemFlags UAVObjectTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return 0;

    if (index.column() == TreeItem::dataColumn) {
        TreeItem *item = static_cast<TreeItem*>(index.internalPointer());
        if (item->isEditable())
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    }

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant UAVObjectTreeModel::headerData(int section, Qt::Orientation orientation,
                                        int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return m_rootItem->data(section);

    return QVariant();
}

void UAVObjectTreeModel::highlightUpdatedObject(UAVObject *obj)
{
    Q_ASSERT(obj);
    ObjectTreeItem *item = findObjectTreeItem(obj);
    Q_ASSERT(item);
    item->setHighlight(true);
    item->update();
    QModelIndex itemIndex = index(item);
    Q_ASSERT(itemIndex != QModelIndex());
    emit dataChanged(itemIndex, itemIndex);
}

ObjectTreeItem *UAVObjectTreeModel::findObjectTreeItem(UAVObject *object)
{
    UAVDataObject *dobj = qobject_cast<UAVDataObject*>(object);
    UAVMetaObject *mobj = qobject_cast<UAVMetaObject*>(object);
    Q_ASSERT(dobj || mobj);
    if (dobj) {
        return findDataObjectTreeItem(dobj);
    } else {
        dobj = qobject_cast<UAVDataObject*>(mobj->getParentObject());
        Q_ASSERT(dobj);
        ObjectTreeItem *dItem = findDataObjectTreeItem(dobj);
        Q_ASSERT(dItem);
        Q_ASSERT(dItem->object());
        if (!dItem->object()->isSingleInstance())
            dItem = dynamic_cast<ObjectTreeItem*>(dItem->parent());
        foreach (TreeItem *child, dItem->treeChildren()) {
            MetaObjectTreeItem *mItem = dynamic_cast<MetaObjectTreeItem*>(child);
            if (mItem && mItem->object()) {
                Q_ASSERT(mItem->object() == mobj);
                return mItem;
            }
        }
    }

    return 0;
}

DataObjectTreeItem *UAVObjectTreeModel::findDataObjectTreeItem(UAVDataObject *object)
{
    Q_ASSERT(object);
    TopTreeItem *root = object->isSettings() ? m_settingsTree : m_nonSettingsTree;
    foreach (TreeItem *child, root->treeChildren()) {
        DataObjectTreeItem *dItem = dynamic_cast<DataObjectTreeItem*>(child);
        if (dItem && dItem->object() && dItem->object()->isSingleInstance()) {
            if(dItem->object() == object) {
                return dItem;
            }
        } else {
            foreach (TreeItem *c, dItem->treeChildren()) {
                DataObjectTreeItem *d = dynamic_cast<DataObjectTreeItem*>(c);
                if (d && d->object() == object)
                    return d;
            }
        }
    }
    return 0;
}

void UAVObjectTreeModel::updateHighlight(TreeItem *item)
{
    QModelIndex itemIndex = index(item);
    Q_ASSERT(itemIndex != QModelIndex());
    emit dataChanged(itemIndex, itemIndex.sibling(itemIndex.row(), TreeItem::dataColumn));
}


