/*
 * SizeGripItem - A size grip QGraphicsItem for interactive resizing.
 *
 * Copyright (c) 2011 Cesar L. B. Silveira
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "sizegripitem.h"
#include <QBrush>
#include <QPen>
#include <QCursor>

SizeGripItem::HandleItem::HandleItem(int positionFlags, SizeGripItem* parent)
    : QGraphicsRectItem(-5, -5, 10, 10, parent),
      positionFlags_(positionFlags),
      parent_(parent)
{
	setPen(Qt::NoPen);
	setBrush(QColor(127, 127, 127, 195));
	setFlag(ItemIsMovable);
	setFlag(ItemSendsGeometryChanges);
	setFlag(ItemIgnoresTransformations);

	switch (positionFlags_)
	{
	case TopLeft:
	case BottomRight:
		setCursor(Qt::SizeFDiagCursor);
		break;
	case TopRight:
	case BottomLeft:
		setCursor(Qt::SizeBDiagCursor);
		break;
	case Top:
	case Bottom:
		setCursor(Qt::SizeVerCursor);
		break;
	case Left:
	case Right:
		setCursor(Qt::SizeHorCursor);
		break;
	}
}

int SizeGripItem::HandleItem::positionFlags() const
{
	return positionFlags_;
}

QVariant SizeGripItem::HandleItem::itemChange(GraphicsItemChange change,
                                              const QVariant &value)
{
	QVariant retVal = value;

	if (change == ItemPositionChange)
	{
		retVal = restrictPosition(value.toPointF());
	}
	else if (change == ItemPositionHasChanged)
	{
		QPointF pos = value.toPointF();

		switch (positionFlags_)
		{
		case TopLeft:
			parent_->setTopLeft(pos);
			break;
		case Top:
			parent_->setTop(pos.y());
			break;
		case TopRight:
			parent_->setTopRight(pos);
			break;
		case Right:
			parent_->setRight(pos.x());
			break;
		case BottomRight:
			parent_->setBottomRight(pos);
			break;
		case Bottom:
			parent_->setBottom(pos.y());
			break;
		case BottomLeft:
			parent_->setBottomLeft(pos);
			break;
		case Left:
			parent_->setLeft(pos.x());
			break;
		}
	}

	return retVal;
}

QPointF SizeGripItem::HandleItem::restrictPosition(const QPointF& newPos)
{
	QPointF retVal = pos();
	const int min = 10; // minimum width/height we allow

	if (positionFlags_ & Top || positionFlags_ & Bottom)
		retVal.setY(newPos.y());

	if (positionFlags_ & Left || positionFlags_ & Right)
		retVal.setX(newPos.x());

	if (positionFlags_ & Top && retVal.y() > parent_->rect_.bottom() - min)
		retVal.setY(parent_->rect_.bottom() - min);
	else if (positionFlags_ & Bottom && retVal.y() < parent_->rect_.top() + min)
		retVal.setY(parent_->rect_.top() + min);

	if (positionFlags_ & Left && retVal.x() > parent_->rect_.right() - min)
		retVal.setX(parent_->rect_.right() - min);
	else if (positionFlags_ & Right && retVal.x() < parent_->rect_.left() + min)
		retVal.setX(parent_->rect_.left() + min);

	return retVal;
}

SizeGripItem::SizeGripItem(Resizer* resizer, QGraphicsItem* parent)
    : QGraphicsObject(parent),
      resizer_(resizer)
{
	if (parentItem())
		rect_ = parentItem()->boundingRect();

	handleItems_.append(new HandleItem(TopLeft, this));
	handleItems_.append(new HandleItem(Top, this));
	handleItems_.append(new HandleItem(TopRight, this));
	handleItems_.append(new HandleItem(Right, this));
	handleItems_.append(new HandleItem(BottomRight, this));
	handleItems_.append(new HandleItem(Bottom, this));
	handleItems_.append(new HandleItem(BottomLeft, this));
	handleItems_.append(new HandleItem(Left, this));
	updateHandleItemPositions();
}

SizeGripItem::~SizeGripItem()
{
	delete resizer_;
}

QRectF SizeGripItem::boundingRect() const
{
	return rect_;
}

void SizeGripItem::paint(QPainter* painter,
                         const QStyleOptionGraphicsItem* option,
                         QWidget* widget)
{
}

#define IMPL_SET_FN(TYPE, POS)                  \
	void SizeGripItem::set ## POS (TYPE v)      \
{                                           \
	rect_.set ## POS (v);                   \
	doResize();                             \
	}

IMPL_SET_FN(qreal, Top)
IMPL_SET_FN(qreal, Right)
IMPL_SET_FN(qreal, Bottom)
IMPL_SET_FN(qreal, Left)
IMPL_SET_FN(const QPointF&, TopLeft)
IMPL_SET_FN(const QPointF&, TopRight)
IMPL_SET_FN(const QPointF&, BottomRight)
IMPL_SET_FN(const QPointF&, BottomLeft)

void SizeGripItem::setRect(QRectF rect)
{
	rect_ = rect;
	updateHandleItemPositions();
}

void SizeGripItem::doResize()
{
	if (resizer_)
	{
		(*resizer_)(parentItem(), rect_);
		updateHandleItemPositions();
	}
}

void SizeGripItem::updateHandleItemPositions()
{
	foreach (HandleItem* item, handleItems_)
	{
		item->setFlag(ItemSendsGeometryChanges, false);

		switch (item->positionFlags())
		{
		case TopLeft:
			item->setPos(rect_.topLeft());
			break;
		case Top:
			item->setPos(rect_.left() + rect_.width() / 2 - 1,
			             rect_.top());
			break;
		case TopRight:
			item->setPos(rect_.topRight());
			break;
		case Right:
			item->setPos(rect_.right(),
			             rect_.top() + rect_.height() / 2 - 1);
			break;
		case BottomRight:
			item->setPos(rect_.bottomRight());
			break;
		case Bottom:
			item->setPos(rect_.left() + rect_.width() / 2 - 1,
			             rect_.bottom());
			break;
		case BottomLeft:
			item->setPos(rect_.bottomLeft());
			break;
		case Left:
			item->setPos(rect_.left(),
			             rect_.top() + rect_.height() / 2 - 1);
			break;
		}

		item->setFlag(ItemSendsGeometryChanges, true);
	}
}
