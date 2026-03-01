package ui

import (
	"image/color"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/widget"
)

type rightClickDetector struct {
	widget.BaseWidget
	onRightTapped func(pos fyne.Position)
}

func newRightClickDetector(onRightTapped func(pos fyne.Position)) *rightClickDetector {
	d := &rightClickDetector{onRightTapped: onRightTapped}
	d.ExtendBaseWidget(d)
	return d
}

func (d *rightClickDetector) TappedSecondary(p *fyne.PointEvent) {
	if d.onRightTapped != nil {
		d.onRightTapped(p.AbsolutePosition)
	}
}

func (d *rightClickDetector) CreateRenderer() fyne.WidgetRenderer {
	r := &canvas.Rectangle{FillColor: color.Transparent}
	return widget.NewSimpleRenderer(r)
}
