from PySide6.QtWidgets import *
from PySide6.QtCore import Qt, QPointF, QEvent, QRect, QSize, QPoint
from PySide6.QtGui import QFont, QPen, QBrush, QPainter, QPainterPath, QPolygonF, QColor, QPalette, QIntValidator, QRegularExpressionValidator, QAction, QPainterPathStroker, QMouseEvent, QValidator
from PySide6.QtCore import QTimer, QRegularExpression, QPropertyAnimation, QEasingCurve, QVariantAnimation
from PySide6.QtWidgets import QGraphicsOpacityEffect
import sys, json, math, os
import re
from pathlib import Path


app_name = "HALO Network Configurator"


DESCRIPTOR_RE = re.compile(
    r'extern\s+const\s+\w+\s+(\w+_DESCRIPTOR)\s*;'
)

MSG_SIZE_RE = re.compile(
    r"const\s+object_desc_t\s+(\w+_DESCRIPTOR)\s*=\s*\{.*?\.message_size\s*=\s*(\d+)",
    re.S
)

def parse_descriptors_from_file(path):
    descriptors = []
    try:
        with open(path, "r") as f:
            text = f.read()
    except OSError:
        return descriptors

    for match in DESCRIPTOR_RE.findall(text):
        message_name = match.replace("_DESCRIPTOR", "")
        descriptors.append({
            "message_descriptor_name": match,
            "message_name": message_name
        })

    return descriptors


def parse_message_sizes_from_c(h_path):
    """
    Given a header path, look for ../Src/<name>.c and extract message_size
    Returns: dict { FULL_DESCRIPTOR_NAME : size }
    """
    try:
        h_path = Path(h_path)
        c_path = h_path.parent.parent / "Src" / f"{h_path.stem}.c"
        if not c_path.exists():
            return {}

        text = c_path.read_text(encoding="utf-8", errors="ignore")
        return {name: int(size) for name, size in MSG_SIZE_RE.findall(text)}
    except Exception:
        return {}



NODE_ID_OPTIONS = [f"0x{n:02X}" for n in range(32)]


def extract_network_model_nodes(network_model_str):
    """Extract number of nodes from network model string (e.g., '32 nodes, 32 local channels' -> 32)"""
    import re
    match = re.search(r'(\d+)\s+nodes?', network_model_str, re.IGNORECASE)
    return int(match.group(1)) if match else 32


def generate_node_id_options(network_model_str):
    """Generate NODE_ID_OPTIONS based on network model setting"""
    num_nodes = extract_network_model_nodes(network_model_str)
    return [f"0x{n:02X}" for n in range(num_nodes)]


def extract_network_model_local_channels(network_model_str):
    """Extract local channel count from network model string (e.g., '32 nodes, 32 local channels' -> 32)."""
    match = re.search(r'(\d+)\s+local\s+channels?', network_model_str, re.IGNORECASE)
    return int(match.group(1)) if match else 32
CHANNEL_CONTROLLER_PLACEHOLDER = "Select Controller Node"
CHANNEL_RESPONDER_PLACEHOLDER = "Select Responder Node"
CHANNEL_NO_NODES_PLACEHOLDER = "No Nodes Available"
CHANNEL_NO_RESPONDERS_PLACEHOLDER = "No Responders Available"
CHANNEL_LANE_MESSAGE_PLACEHOLDER = "Select Message"
CHANNEL_NO_MESSAGES_PLACEHOLDER = "No Messages Available"
CHANNEL_NAME_PLACEHOLDER = "Enter Channel Name"
CHANNEL_GLOBAL_ID_PLACEHOLDER = "Auto"
CHANNEL_LOCAL_ID_PLACEHOLDER = "Auto"
CHANNEL_TIMEOUT_PLACEHOLDER = "Enter Timeout (μs)"
NODE_NAME_PLACEHOLDER = "Enter Node Name"
NODE_ID_PLACEHOLDER = "Select Node ID"
NODE_RADIUS = 24
COLUMN_SPACING = 150
ROW_SPACING = 80


def flash_block_border(block, color=QColor(Qt.GlobalColor.white), duration=500, width=4, radius=5, flashes=1):
    """
    Flash only the border of the given QWidget/block with rounded corners.
    If a flash is already active, it restarts the fade.
    
    :param block: The QWidget to flash.
    :param color: Border color.
    :param duration: Total flash duration in ms.
    :param width: Border width in pixels.
    :param radius: Corner radius in pixels.
    :param flashes: Number of times to flash (1 or 2).
    """
    step_ms = 25
    steps = max(1, duration // step_ms)

    # store original paintEvent if not already stored
    if not hasattr(block, "_orig_paint_event"):
        block._orig_paint_event = block.paintEvent

    # prevent stacking flashes - stop old timer and restart
    if hasattr(block, "_flash_timer") and block._flash_timer.isActive():
        block._flash_timer.stop()
        if hasattr(block, "_flash_alpha"):
            del block._flash_alpha
        if hasattr(block, "_flash_count"):
            del block._flash_count
        if hasattr(block, "_max_flashes"):
            del block._max_flashes

    # override paintEvent temporarily
    def paint(event):
        block._orig_paint_event(event)
        if getattr(block, "_flash_alpha", 0) > 0:
            painter = QPainter(block)
            pen = QPen(QColor(color))
            pen.setWidth(width)
            c = QColor(color)
            c.setAlpha(int(block._flash_alpha))
            pen.setColor(c)
            painter.setPen(pen)
            painter.drawRoundedRect(block.rect().adjusted(1, 1, -1, -1), radius, radius)

    block.paintEvent = paint
    block._flash_alpha = 255
    block._flash_count = 0
    block._max_flashes = flashes

    timer = QTimer(block)

    def on_timeout():
        if not hasattr(block, "_flash_alpha"):
            timer.stop()
            return
        block._flash_alpha -= 255 / steps
        block.update()
        if block._flash_alpha <= 0:
            block._flash_count += 1
            if block._flash_count < block._max_flashes:
                block._flash_alpha = 255  # Reset for next flash
            else:
                timer.stop()
                block.paintEvent = block._orig_paint_event
                if hasattr(block, "_flash_alpha"):
                    del block._flash_alpha
                if hasattr(block, "_flash_count"):
                    del block._flash_count
                if hasattr(block, "_max_flashes"):
                    del block._max_flashes
                if hasattr(block, "_flash_timer"):
                    del block._flash_timer

    timer.timeout.connect(on_timeout)
    timer.start(step_ms)
    block._flash_timer = timer

from PySide6.QtWidgets import QGraphicsView, QGraphicsPathItem, QGraphicsPolygonItem
from PySide6.QtGui import QPainter, QPen, QColor, QPainterPath, QPolygonF
from PySide6.QtCore import Qt, QPointF
import math

# --- Helper function ---
def distance_path_to_point(path, point, steps=100):
    """Approximate distance from a QPainterPath to a point by sampling points along the path."""
    min_dist = float("inf")
    for i in range(steps + 1):
        p = path.pointAtPercent(i / steps)
        dist = math.hypot(p.x() - point.x(), p.y() - point.y())
        if dist < min_dist:
            min_dist = dist
    return min_dist


class GraphView(QGraphicsView):
    HOVER_WIDTH = 2
    def __init__(self, scene, app):
        super().__init__(scene)
        self.app = app
        self.setRenderHint(QPainter.Antialiasing)
        self.setMouseTracking(True)  # needed for hover

    def edge_under_cursor(self, pos: QPointF):
        """Return the ChannelEdge under pos, closest to cursor along its path."""
        edges = [item for item in self.scene().items() if isinstance(item, ChannelEdge)]
        edges_containing = [e for e in edges if e.shape().contains(pos)]
        if not edges_containing:
            return None
        # Pick the edge with the closest point along its path
        def distance2(edge):
            path = edge.path()
            return min((pos - path.pointAtPercent(i/100)).manhattanLength() for i in range(101))
        return min(edges_containing, key=distance2)

    def node_under_cursor(self, pos: QPointF):
        """Return the NodeGraphic under pos."""
        nodes = [item for item in self.scene().items() if isinstance(item, NodeGraphic)]
        for node in nodes:
            if node.shape().contains(pos):
                return node
        return None

    def mousePressEvent(self, event):
        # First, let Qt handle the event and route it to graphics items
        super().mousePressEvent(event)
        
        # If the event was accepted by a graphics item, we're done
        if event.isAccepted():
            return
        
        # Otherwise, handle empty space clicks (clear selection)
        if self.app.selected_edge:
            self.app.selected_edge.set_selected(False)
            self.app.selected_edge = None
        if self.app.selected_node:
            self.app.selected_node.set_selected(False)
            self.app.selected_node = None
        self.app.graph_info.hide()

    def mouseMoveEvent(self, event):
        pos = self.mapToScene(event.position().toPoint())
        edge = self.edge_under_cursor(pos)
        
        # Handle edge hover
        for item in self.scene().items():
            if isinstance(item, ChannelEdge):
                if item is edge:
                    if not getattr(item, "_hovered", False):
                        item._hovered = True
                        item.hoverEnterEvent(None)
                else:
                    if getattr(item, "_hovered", False):
                        item._hovered = False
                        item.hoverLeaveEvent(None)
        
        # Let Qt handle node hovering naturally since NodeGraphic has setAcceptHoverEvents(True)
        super().mouseMoveEvent(event)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self.app:
            QTimer.singleShot(0, self.app.update_graph)



class ChannelEdge(QGraphicsPathItem):
    NORMAL_COLOR = QColor(Qt.GlobalColor.darkCyan)
    HOVER_COLOR = QColor(Qt.GlobalColor.cyan)
    SELECTED_COLOR = QColor(Qt.GlobalColor.cyan)
    HOVER_SET_WIDTH = 2

    def __init__(self, path, channel, app):
        super().__init__(path)
        self.channel = channel
        self.app = app

        self.setPen(QPen(self.NORMAL_COLOR, 2))
        self.setZValue(-1)  # Behind nodes
        self.setAcceptHoverEvents(True)

        # Store original path for safe updates
        self.full_path = QPainterPath(path)

        # Arrowhead
        self.arrow = QGraphicsPolygonItem(self)
        self.arrow.setPen(Qt.NoPen)
        self.arrow.setBrush(self.NORMAL_COLOR)
        self.update_arrow()

    def update_arrow(self):
        """Update arrowhead pointing toward node and adjust path without shrinking repeatedly."""
        path = QPainterPath(self.full_path)

        # Compute direction at end
        end_pos = path.pointAtPercent(1)
        prev_pos = path.pointAtPercent(0.98)
        dx, dy = end_pos.x() - prev_pos.x(), end_pos.y() - prev_pos.y()
        length = math.hypot(dx, dy)
        if length == 0:
            return
        ux, uy = dx / length, dy / length

        arrow_size = 4 + self.pen().widthF() * 2
        end_adjusted = QPointF(end_pos.x() - ux * arrow_size, end_pos.y() - uy * arrow_size)

        # Rebuild path to stop just before arrow
        new_path = QPainterPath(path.pointAtPercent(0))
        for i in range(1, 101):
            p = path.pointAtPercent(i / 100)
            if (end_pos - p).manhattanLength() <= arrow_size:
                break
            new_path.lineTo(p)
        self.setPath(new_path)

        # Arrow polygon
        angle = math.pi / 6
        p1 = end_pos
        p2 = QPointF(
            end_pos.x() - arrow_size * (ux * math.cos(angle) + uy * math.sin(angle)),
            end_pos.y() - arrow_size * (uy * math.cos(angle) - ux * math.sin(angle))
        )
        p3 = QPointF(
            end_pos.x() - arrow_size * (ux * math.cos(angle) - uy * math.sin(angle)),
            end_pos.y() - arrow_size * (uy * math.cos(angle) + ux * math.sin(angle))
        )
        self.arrow.setPolygon(QPolygonF([p1, p2, p3]))
        self.arrow.setBrush(self.pen().color())

    def set_selected(self, selected: bool):
        self.selected = selected
        color = self.SELECTED_COLOR if selected else self.NORMAL_COLOR
        pen = self.pen()
        pen.setColor(color)
        pen.setWidth(GraphView.HOVER_WIDTH if selected else 2)
        self.setPen(pen)
        self.arrow.setBrush(color)
        self.update_arrow()


    def hoverEnterEvent(self, event):
        if self is not self.app.selected_edge:
            pen = self.pen()
            pen.setColor(self.HOVER_COLOR)
            pen.setWidth(self.HOVER_SET_WIDTH)
            self.setPen(pen)
            self.arrow.setBrush(self.HOVER_COLOR)
            self.update_arrow()

    def hoverLeaveEvent(self, event):
        if self is not self.app.selected_edge:
            pen = self.pen()
            pen.setColor(self.NORMAL_COLOR)
            pen.setWidth(2)
            self.setPen(pen)
            self.arrow.setBrush(self.NORMAL_COLOR)
            self.update_arrow()

    def mousePressEvent(self, event):
        self.app.select_edge(self)
        event.accept()

    def set_hover(self, hover: bool):
        if hover:
            pen = self.pen()
            pen.setColor(self.HOVER_COLOR)
            pen.setWidth(GraphView.HOVER_WIDTH)
            self.setPen(pen)
            self.arrow.setBrush(self.HOVER_COLOR)
        else:
            color = self.SELECTED_COLOR if getattr(self, 'selected', False) else self.NORMAL_COLOR
            pen = self.pen()
            pen.setColor(color)
            pen.setWidth(2)
            self.setPen(pen)
            self.arrow.setBrush(color)
        self.update_arrow()

class NodeGraphic(QGraphicsEllipseItem):
    NORMAL_COLOR = QColor(Qt.GlobalColor.darkCyan)
    HOVER_COLOR = QColor(Qt.GlobalColor.cyan)
    SELECTED_COLOR = QColor(Qt.GlobalColor.cyan)
    NORMAL_BORDER = 2
    HOVER_BORDER = 3
    SELECTED_BORDER = 3
    
    def __init__(self, node_block, app, text_items=None):
        # Create ellipse from bounds
        super().__init__(-NODE_RADIUS, -NODE_RADIUS, 2*NODE_RADIUS, 2*NODE_RADIUS)
        self.node_block = node_block
        self.app = app
        self.text_items = text_items or []  # List of text items to highlight
        # Store original colors of text items to restore on unhover
        self.text_original_colors = {text: text.defaultTextColor() for text in self.text_items}
        
        self.setPen(QPen(self.NORMAL_COLOR, self.NORMAL_BORDER))
        self.setBrush(QBrush(Qt.GlobalColor.lightGray))
        self.setAcceptHoverEvents(True)
        self.setZValue(0)
    
    def set_selected(self, selected: bool):
        self.selected = selected
        pen = self.pen()
        if selected:
            pen.setColor(self.SELECTED_COLOR)
            pen.setWidth(self.SELECTED_BORDER)
            # Highlight text
            for text in self.text_items:
                text.setDefaultTextColor(self.SELECTED_COLOR)
        else:
            pen.setColor(self.NORMAL_COLOR)
            pen.setWidth(self.NORMAL_BORDER)
            # Restore text to original colors
            for text in self.text_items:
                text.setDefaultTextColor(self.text_original_colors[text])
        self.setPen(pen)
    
    def hoverEnterEvent(self, event):
        if not getattr(self, 'selected', False):
            pen = self.pen()
            pen.setColor(self.HOVER_COLOR)
            pen.setWidth(self.HOVER_BORDER)
            self.setPen(pen)
            # Highlight text on hover
            for text in self.text_items:
                text.setDefaultTextColor(self.HOVER_COLOR)
            self.update()
    
    def hoverLeaveEvent(self, event):
        if not getattr(self, 'selected', False):
            pen = self.pen()
            pen.setColor(self.NORMAL_COLOR)
            pen.setWidth(self.NORMAL_BORDER)
            self.setPen(pen)
            # Restore text to original colors
            for text in self.text_items:
                text.setDefaultTextColor(self.text_original_colors[text])
            self.update()
    
    def mousePressEvent(self, event):
        self.app.select_node(self.node_block)
        event.accept()

class NoScrollComboBox(QComboBox):
    """ComboBox that ignores mouse wheel events."""
    def wheelEvent(self, event):
        event.ignore()
    
    def has_selection(self):
        """Check if the combo box has a real selection (not showing placeholder)."""
        return bool(self.currentText() and self.currentIndex() >= 0)

    def paintEvent(self, event):
        painter = QStylePainter(self)
        opt = QStyleOptionComboBox()
        self.initStyleOption(opt)

        # 1. Determine which text to display (Actual text vs Placeholder)
        actual_text = self.currentText()
        is_placeholder = False

        if not actual_text and hasattr(self, "placeholderText"):
            actual_text = self.placeholderText()
            is_placeholder = True

        # 2. Get the drawing area (excluding the arrow)
        rect = self.style().subControlRect(
            QStyle.CC_ComboBox, 
            opt, 
            QStyle.SC_ComboBoxEditField, 
            self
        )

        # 3. Elide the chosen text
        available_width = rect.width() - 20
        elided_text = opt.fontMetrics.elidedText(
            actual_text, 
            Qt.ElideRight, 
            available_width
        )

        # 4. Update the option with elided text
        opt.currentText = elided_text
        
        # 5. If it's a placeholder, we should dim the color to look "disabled"
        if is_placeholder:
            opt.palette.setCurrentColorGroup(QPalette.Disabled)

        # 6. Draw it
        painter.drawComplexControl(QStyle.CC_ComboBox, opt)
        painter.drawControl(QStyle.CE_ComboBoxLabel, opt)

def add_row(parent_layout, label_text, widget, label_width):
    """Add a label and a field on the same horizontal row."""
    row = QHBoxLayout()
    label = QLabel(label_text)
    label.setFixedWidth(label_width)  # keeps all labels aligned
    row.addWidget(label)
    row.addWidget(widget, stretch=1)
    widget.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
    widget.setFixedHeight(24)
    parent_layout.addLayout(row)


# ---------- Custom Callout Widget ----------
class ErrorCallout(QWidget):
    """A callout widget with a pointing tail, black background, red border, white text"""
    def __init__(self, parent=None, floating=False):
        super().__init__(parent)
        self.setVisible(False)
        self._bg_color = QColor(0, 0, 0)
        self._text_color = QColor(255, 255, 255)
        self._floating = floating  # Track if this is a floating callout
        
        # Create layout with label
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 4, 8, 4)
        layout.setSpacing(0)
        
        self.label = QLabel()
        self.label.setStyleSheet("font-size: 10px; font-weight: bold;")
        self.label.setWordWrap(False)
        self.label.setAlignment(Qt.AlignCenter)
        self.label.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Minimum)
        layout.addWidget(self.label, stretch=0, alignment=Qt.AlignCenter)
        
        self._tail_height = 0
        self._tail_width = 0
        self.setLayout(layout)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setAutoFillBackground(False)
        
        # Set size policy to support height-for-width
        self.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Minimum)
        self.label.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Minimum)
        
    def setText(self, text):
        self.label.setText(text)
        # Force complete layout update first
        self.label.updateGeometry()
        self.updateGeometry()
        if self.parentWidget():
            self.parentWidget().updateGeometry()
        
        # Schedule height recalculation after layout settles
        QTimer.singleShot(0, self._recalculate_height)
    
    def _recalculate_height(self):
        """Recalculate and set minimum height after layout has determined width"""
        if self.width() > 10:  # Only if we have a valid width
            required_height = self.heightForWidth(self.width())
            self.setMinimumHeight(required_height)
            self.setMaximumHeight(required_height)

    def set_colors(self, background_color, text_color=None):
        self._bg_color = background_color
        self._text_color = text_color if text_color is not None else QColor(255, 255, 255)
        self.label.setStyleSheet(
            f"color: {self._text_color.name()}; font-size: 10px; font-weight: bold;"
        )
    
    def hasHeightForWidth(self):
        return True
    
    def heightForWidth(self, width):
        # Calculate height needed for this width
        margins = self.layout().contentsMargins()
        shadow_offset = 4 if self._floating else 0
        border_width = 2
        
        # Since word wrap is disabled, use the label's natural height
        label_height = self.label.sizeHint().height()
        
        total_height = label_height + margins.top() + margins.bottom() + shadow_offset + border_width
        return int(total_height)
    
    def sizeHint(self):
        # Calculate proper size
        margins = self.layout().contentsMargins()
        shadow_offset = 4 if self._floating else 0
        
        label_height = self.label.sizeHint().height()
        total_height = label_height + margins.top() + margins.bottom() + shadow_offset + 2
        total_width = self.label.sizeHint().width() + margins.left() + margins.right() + shadow_offset + 2
        
        return QSize(total_width, total_height)
    
    def paintEvent(self, event):
        """Draw callout with rounded corners, red border, and shadow"""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        
        radius = 3

        # Draw shadow (only for floating callouts)
        max_shadow = 4 if self._floating else 0
        border_inset = 1
        body_rect = QRect(
            border_inset,
            border_inset,
            self.width() - max_shadow - border_inset * 2,
            self.height() - max_shadow - border_inset * 2,
        )
        if self._floating:
            for offset, alpha in ((4, 35), (2, 22)):
                shadow_rect = body_rect.translated(offset, offset)
                shadow_path = QPainterPath()
                shadow_path.addRoundedRect(shadow_rect, radius, radius)
                painter.fillPath(shadow_path, QColor(0, 0, 0, alpha))

        # Draw the main rectangle body with all rounded corners
        rect = body_rect
        path_rect = QPainterPath()
        path_rect.addRoundedRect(rect, radius, radius)
        painter.fillPath(path_rect, self._bg_color)

        # Red border
        pen_border = QPen(QColor(255, 0, 0), 1)
        painter.setPen(pen_border)
        painter.drawPath(path_rect)
        
        painter.end()
        
        # Draw children (label)
        super().paintEvent(event)


def add_row(parent_layout, label_text, widget, label_width):
    """Add a label and a field on the same horizontal row."""
    row = QHBoxLayout()
    label = QLabel(label_text)
    label.setFixedWidth(label_width)  # keeps all labels aligned
    row.addWidget(label)
    row.addWidget(widget, stretch=1)
    widget.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
    widget.setFixedHeight(24)
    parent_layout.addLayout(row)


def add_name_row(parent_layout, label_text, widget, label_width):
    """
    Add a label, name field, and error callout within the layout.
    Returns the error callout for later reference.
    """
    row = QHBoxLayout()
    label = QLabel(label_text)
    label.setFixedWidth(label_width)
    row.addWidget(label)
    row.addWidget(widget, stretch=1)
    widget.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
    widget.setFixedHeight(24)
    parent_layout.addLayout(row)
    
    # Create error callout aligned with the input field
    error_row = QHBoxLayout()
    error_row.setContentsMargins(0, 0, 2, 0)
    error_callout = ErrorCallout()
    error_callout._input_field = widget
    field_bg = widget.palette().color(QPalette.Base)
    field_text = widget.palette().color(QPalette.Text)
    error_callout.set_colors(field_bg, field_text)

    error_row.addWidget(error_callout)
    parent_layout.addLayout(error_row)
    
    return error_callout


# ---------- Node Block ----------
class NodeBlock(QFrame):
    def __init__(self, index, app):
        super().__init__()
        self.app = app

        self.setObjectName("NodeBlockFrame")
        self.setFrameShape(QFrame.Box)

        # Use Qt colors
        bg_color = QColor(Qt.darkGray)
        border_color = QColor(Qt.darkCyan)

        # Apply stylesheet using variables
        self.setStyleSheet(f"""
            QFrame#NodeBlockFrame {{
                background-color: {bg_color.name()};
                border: 2px solid {border_color.name()};
                border-radius: 5px;
            }}
        """)
     
        layout = QVBoxLayout()
        top = QHBoxLayout()
        self.title = QLabel(f"Node {index}")
        self.title.setStyleSheet("font-weight: bold")
        top.addWidget(self.title)

        delete_btn = QPushButton("✕")
        delete_btn.setFixedWidth(25)
        delete_btn.clicked.connect(self.delete)
        top.addWidget(delete_btn)
        layout.addLayout(top)

        label_width = 80  # fixed width for labels

        node_field_bg = self.app.view.palette().color(QPalette.Button).lighter(110)

        # NodeBlock
        self.name_edit = QLineEdit()
        self.name_edit.setPlaceholderText(NODE_NAME_PLACEHOLDER)
        # Allow empty string (when field is cleared) or valid identifier
        regex = QRegularExpression("^([a-zA-Z_][a-zA-Z0-9_]*)?$")
        validator = QRegularExpressionValidator(regex)
        self.name_edit.setValidator(validator)
        self.name_error_label = add_name_row(layout, "Node Name:", self.name_edit, label_width)

        palette = self.name_edit.palette()
        palette.setColor(QPalette.Base, node_field_bg)   
        self.name_edit.setPalette(palette)
        self.name_edit.setObjectName("NodeNameEdit")

        self.node_id_cb = NoScrollComboBox()
        self.node_id_cb.setInsertPolicy(QComboBox.NoInsert)
        self.node_id_cb.setPlaceholderText(NODE_ID_PLACEHOLDER)
        self.node_id_cb.addItems(NODE_ID_OPTIONS) # Must be placed after placeholder or it won't show

        add_row(layout, "Node ID:", self.node_id_cb, label_width)
        self.listener_cb = QCheckBox("Listener (passive, receive-only)")
        self.listener_cb.setToolTip("Passive node: observes all channels, never transmits")
        layout.addWidget(self.listener_cb)
        self.node_id_cb.setStyleSheet(f"""
        QComboBox {{
            background-color: {node_field_bg.name()};
       
            padding: 2px 5px;
        }}
        QComboBox QAbstractItemView {{
            background-color: {node_field_bg.name()};
            selection-background-color: {node_field_bg.darker().name()};
            selection-color: {node_field_bg.lighter().name()};
        }}
    """)

        # Channel tracking fields
        self.channel_count = 0  # Number of channels this node is connected to
        self.controller_hierarchy_rank = -1  # Rank in controller hierarchy (-1 = not a controller, 0+ = rank)

        self.setLayout(layout)

        # Connect signals AFTER setup
        self.node_id_cb.currentIndexChanged.connect(lambda *_: self.app.recompute_channels())
        self.node_id_cb.currentIndexChanged.connect(lambda *_: self.app.update_node_ids(self))
        self.listener_cb.toggled.connect(lambda *_: self.app.on_listener_toggled(self))

        #-- Connect editingFinished signals to recompute on focus loss for line edits (avoids too many updates)
        self.name_edit.editingFinished.connect(lambda *_: [
            self.app.recompute_channels(self),
            self.app.update_node_ids(),
            self.app.validate_unique_names(),
            self.app.show_invalid_name_callout(self.name_edit),
            print("Name edit finished")
        ])
        self.name_edit.inputRejected.connect(lambda: self.app.show_invalid_name_callout(
            self.name_edit,
            force=True))
        
        # Node metrics
        self.channel_count = 0
        self.controller_hierarchy_rank = -1
        self.connected_channels = []  # List of channel names this node is connected to
        self.controllers = []  # List of controller node names for this node
        self.responders = []  # List of responder node names for this node
        

        self.controller_endpoints_count = 0  # Number of channels where this node is controller
        self.responder_endpoints_count = 0   # Number of channels where this node is responder
        self.listener_endpoints_count = 0    # Number of channels where this node is listener

    def is_listener(self):
        return self.listener_cb.isChecked()   

    def delete(self):
        self.app.delete_node(self)

    def set_index(self, idx):
        self.title.setText(f"Node {idx}")

    def get_data(self):
        return {
            "name": self.name_edit.text(),
            "node_id": self.node_id_cb.currentText(),
            "channel_count": self.channel_count,
            "controller_endpoints_count": self.controller_endpoints_count,
            "responder_endpoints_count": self.responder_endpoints_count,
            "controller_hierarchy_rank": self.controller_hierarchy_rank,
            "connected_channels": self.connected_channels,
            "controllers": self.controllers,
            "responders": self.responders,
            "is_listener": self.is_listener(),
            "listener_endpoints_count": self.listener_endpoints_count
        }


# ---------- Channel Block ----------
class ChannelBlock(QFrame):
    def __init__(self, index, app):
        super().__init__()
        self.app = app
        self.setFrameShape(QFrame.Box)

        self.setObjectName("ChannelBlockFrame")
        self.setFrameShape(QFrame.Box)

        # Use Qt colors
        bg_color = QColor(Qt.darkGray)
        border_color = QColor(Qt.darkCyan)

        # Apply stylesheet using variables
        self.setStyleSheet(f"""
            QFrame#ChannelBlockFrame {{
                background-color: {bg_color.name()};
                border: 2px solid {border_color.name()};
                border-radius: 5px;
            }}
        """)

        layout = QVBoxLayout()
        top = QHBoxLayout()
        self.title = QLabel(f"Channel {index}")
        self.title.setStyleSheet("font-weight: bold")
        top.addWidget(self.title)

        delete_btn = QPushButton("✕")
        delete_btn.setFixedWidth(25)
        delete_btn.clicked.connect(self.delete)
        top.addWidget(delete_btn)
        layout.addLayout(top)

        label_width = 124  # fixed width for labels
        node_field_bg = self.app.view.palette().color(QPalette.Button).lighter(110)
        node_locked_field_bg = self.app.view.palette().color(QPalette.Midlight).lighter(110)

        self.name_edit = QLineEdit()
        self.name_edit.setPlaceholderText(CHANNEL_NAME_PLACEHOLDER)
        # Allow empty string (when field is cleared) or valid identifier
        regex = QRegularExpression("^([a-zA-Z_][a-zA-Z0-9_]*)?$")
        validator = QRegularExpressionValidator(regex)
        self.name_edit.setValidator(validator)
        self.name_error_label = add_name_row(layout, "Channel Name:", self.name_edit, label_width)
        palette = self.name_edit.palette()
        palette.setColor(QPalette.Base, node_field_bg)   
        self.name_edit.setPalette(palette)
        self.name_edit.setObjectName("ChannelNameEdit")

        self.global_edit = QLineEdit(); 
        self.global_edit.setReadOnly(True); 
        self.global_edit.setPlaceholderText(CHANNEL_GLOBAL_ID_PLACEHOLDER)
        add_row(layout, "Global Channel ID:", self.global_edit, label_width)
        palette = self.global_edit.palette()
        palette.setColor(QPalette.Base, node_locked_field_bg)   
        self.global_edit.setPalette(palette)

        self.local_edit = QLineEdit(); 
        self.local_edit.setReadOnly(True); 
        self.local_edit.setPlaceholderText(CHANNEL_LOCAL_ID_PLACEHOLDER)
        add_row(layout, "Local Channel ID:", self.local_edit, label_width)
        palette = self.local_edit.palette()
        palette.setColor(QPalette.Base, node_locked_field_bg)   
        self.local_edit.setPalette(palette)

        self.controller_cb =NoScrollComboBox(); 
        add_row(layout, "Controller Node:", self.controller_cb, label_width)
        self.controller_cb.setInsertPolicy(QComboBox.NoInsert)
        self.controller_cb.setPlaceholderText(CHANNEL_CONTROLLER_PLACEHOLDER)

        self.controller_cb.setStyleSheet(f"""
                QComboBox {{
                    background-color: {node_field_bg.name()};
                    padding: 2px 5px;
                }}
                QComboBox QAbstractItemView {{
                    background-color: {node_field_bg.name()};
                    selection-background-color: {node_field_bg.darker().name()};
                    selection-color: {node_field_bg.lighter().name()};
                }}
            """)

        self.responder_cb = NoScrollComboBox(); 
        add_row(layout, "Responder Node:", self.responder_cb, label_width)
        self.responder_cb.setInsertPolicy(QComboBox.NoInsert)
        self.responder_cb.setPlaceholderText(CHANNEL_RESPONDER_PLACEHOLDER)
        self.responder_cb.setStyleSheet(f"""
                QComboBox {{
                    background-color: {node_field_bg.name()};
                    padding: 2px 5px;
                }}
                QComboBox QAbstractItemView {{
                    background-color: {node_field_bg.name()};
                    selection-background-color: {node_field_bg.darker().name()};
                    selection-color: {node_field_bg.lighter().name()};
                }}
            """)

        self.lane0_msg_cb = NoScrollComboBox()
        self.lane0_msg_cb.setInsertPolicy(QComboBox.NoInsert)
        self.lane0_msg_cb.setPlaceholderText(CHANNEL_LANE_MESSAGE_PLACEHOLDER)
        self.lane0_msg_cb.setStyleSheet(f"""
                QComboBox {{
                    background-color: {node_field_bg.name()};
                    padding: 2px 5px;
                }}
                QComboBox QAbstractItemView {{
                    background-color: {node_field_bg.name()};
                    selection-background-color: {node_field_bg.darker().name()};
                    selection-color: {node_field_bg.lighter().name()};
                }}
            """)
        add_row(layout, "Lane 0 Message:", self.lane0_msg_cb, label_width)
        palette = self.lane0_msg_cb.palette()
        palette.setColor(QPalette.Base, node_field_bg)   
        self.lane0_msg_cb.setPalette(palette)

        self.lane0_to_edit = QLineEdit(); 
        self.lane0_to_edit.setPlaceholderText(CHANNEL_TIMEOUT_PLACEHOLDER)
        # Allow empty string or number 0-999999
        timeout_regex = QRegularExpression("^([0-9]{1,6})?$")
        self.lane0_to_edit.setValidator(QRegularExpressionValidator(timeout_regex))
        add_row(layout, "Lane 0 Timeout:", self.lane0_to_edit, label_width)
        palette = self.lane0_to_edit.palette()
        palette.setColor(QPalette.Base, node_field_bg)   
        self.lane0_to_edit.setPalette(palette)

        self.lane1_msg_cb = NoScrollComboBox()
        self.lane1_msg_cb.setInsertPolicy(QComboBox.NoInsert)
        self.lane1_msg_cb.setPlaceholderText(CHANNEL_LANE_MESSAGE_PLACEHOLDER)
        self.lane1_msg_cb.setStyleSheet(f"""
                QComboBox {{
                    background-color: {node_field_bg.name()};
                    padding: 2px 5px;
                }}
                QComboBox QAbstractItemView {{
                    background-color: {node_field_bg.name()};
                    selection-background-color: {node_field_bg.darker().name()};
                    selection-color: {node_field_bg.lighter().name()};
                }}
            """)
        add_row(layout, "Lane 1 Message:", self.lane1_msg_cb, label_width)
        palette = self.lane1_msg_cb.palette()
        palette.setColor(QPalette.Base, node_field_bg)   
        self.lane1_msg_cb.setPalette(palette)


        self.setLayout(layout)

        # Connect editingFinished signals to recompute on focus loss for line edits (avoids too many updates)
        if isinstance(self.lane0_to_edit, QLineEdit):
            self.lane0_to_edit.editingFinished.connect(lambda w=self.lane0_to_edit: self.app.recompute_channels(self))
        
        # Name field also triggers validation
        self.name_edit.editingFinished.connect(lambda: [
            self.app.recompute_channels(self),
            self.app.validate_unique_names(),
            self.app.show_invalid_name_callout(self.name_edit)
        ])
        self.name_edit.inputRejected.connect(lambda: self.app.show_invalid_name_callout(
            self.name_edit,
            force=True))

        # Timeout fields: show callout when invalid
        self.lane0_to_edit.editingFinished.connect(lambda: self.app.show_invalid_input_callout(
            self.lane0_to_edit,
            "Invalid timeout. Enter a number between 0 and 999999."))
        self.lane0_to_edit.inputRejected.connect(lambda: self.app.show_invalid_input_callout(
            self.lane0_to_edit,
            "Invalid timeout. Enter a number between 0 and 999999.",
            force=True))
        
        # Reset palette when user starts typing to give immediate feedback
        def reset_timeout_palette(line_edit):
            if hasattr(line_edit, '_original_palette'):
                line_edit.setPalette(line_edit._original_palette)
                delattr(line_edit, '_original_palette')
            if hasattr(line_edit, '_cleanup_timer'):
                line_edit._cleanup_timer.stop()
                delattr(line_edit, '_cleanup_timer')
            if hasattr(line_edit, '_error_callout'):
                line_edit._error_callout.deleteLater()
                delattr(line_edit, '_error_callout')
        
        self.lane0_to_edit.editingFinished.connect(lambda: [
            reset_timeout_palette(self.lane0_to_edit),
            self.app.recompute_channels(self)
        ])

        # Connect signals AFTER setup
        for widget in [self.controller_cb, self.responder_cb, self.lane0_msg_cb, self.lane1_msg_cb]:
            signal = getattr(widget, "textChanged", None) or getattr(widget, "currentIndexChanged", None)
            if signal:
                signal.connect(lambda *_: self.app.recompute_channels(self))
        
                self.controller_cb.currentIndexChanged.connect(lambda *_: self.update_nodes(self.app.endpoint_node_names()))

    def delete(self):
        self.app.delete_channel(self)

    def set_index(self, idx):
        self.title.setText(f"Channel {idx}")

    def update_nodes(self, names):
        cur_controller = self.controller_cb.currentText()
        cur_responder = self.responder_cb.currentText()
        nodes = self.app.get_nodes()
        priority_model = self.app.settings.get("priority_model")
        is_global = priority_model == "Global Hierarchical"
        
        # For Hybrid mode, check if this controller's rank is within depth threshold
        if priority_model == "Hybrid Hierarchical" and cur_controller:
            # Find the controller node block to get its rank
            for b in self.app.node_blocks:
                if b.name_edit.text() == cur_controller:
                    depth_threshold = self.app.settings.get("priority_model_depth", 0)
                    is_global = b.controller_hierarchy_rank != -1 and b.controller_hierarchy_rank <= depth_threshold
                    break

        self.controller_cb.blockSignals(True)
        self.responder_cb.blockSignals(True)

        # --- Filter valid controllers for global hierarchy ---
        valid_controllers = list(names)
        if priority_model in ("Global Hierarchical", "Hybrid Hierarchical"):
            # Collect committed global-tier (controller_id, responder_id) pairs from other channels
            committed_pairs = []
            for ch in self.app.channel_blocks:
                if ch is self:
                    continue
                m = ch.controller_cb.currentText()
                s = ch.responder_cb.currentText()
                if m in nodes and s in nodes:
                    is_ch_global = priority_model == "Global Hierarchical"
                    if priority_model == "Hybrid Hierarchical":
                        cb = next((b for b in self.app.node_blocks if b.name_edit.text() == m), None)
                        dt = self.app.settings.get("priority_model_depth", 0)
                        is_ch_global = cb and cb.controller_hierarchy_rank != -1 and cb.controller_hierarchy_rank <= dt
                    if is_ch_global:
                        committed_pairs.append((nodes[m], nodes[s]))

            def is_valid_controller(n):
                if n not in nodes:
                    return True
                nid = nodes[n]
                # Only check responders of controllers with strictly lower ID (higher priority)
                higher_resps = [sid for mid, sid in committed_pairs if mid < nid]
                return not higher_resps or nid >= max(higher_resps)

            valid_controllers = [n for n in names if is_valid_controller(n)]

        # --- Update controller combo ---
        self.controller_cb.clear()
        self.controller_cb.addItems(valid_controllers)
        if cur_controller in valid_controllers:
            self.controller_cb.setCurrentText(cur_controller)
        else:
            self.controller_cb.setCurrentIndex(-1)
            self.controller_cb.setPlaceholderText(CHANNEL_CONTROLLER_PLACEHOLDER)

        if not valid_controllers:
            self.controller_cb.setCurrentIndex(-1)
            self.responder_cb.setCurrentIndex(-1)
            self.controller_cb.setPlaceholderText(CHANNEL_NO_NODES_PLACEHOLDER)
            self.responder_cb.setPlaceholderText(CHANNEL_NO_NODES_PLACEHOLDER)
            self.controller_cb.blockSignals(False)
            self.responder_cb.blockSignals(False)
            return

        if cur_controller not in nodes:
            self.responder_cb.clear()
            self.responder_cb.setPlaceholderText(CHANNEL_NO_RESPONDERS_PLACEHOLDER)
            self.controller_cb.blockSignals(False)
            self.responder_cb.blockSignals(False)
            return

        controller_id = nodes[cur_controller]

        # --- Compute global hierarchy check ---
        max_responder_check = None
        depth_threshold = self.app.settings.get("priority_model_depth", 0) if priority_model == "Hybrid Hierarchical" else None
        
        # For Global mode or Hybrid mode with controller in global tier
        if is_global:
            # Look at all channels where controller ID < cur_controller ID
            candidate_responders = []
            for ch in self.app.channel_blocks:
                m = ch.controller_cb.currentText()
                s = ch.responder_cb.currentText()
                if m in nodes and s in nodes:
                    if nodes[m] < controller_id:    
                        candidate_responders.append(nodes[s])
            if candidate_responders:
                max_responder_check = max(candidate_responders) # highest responder ID among those channels (lowest priority responder)
        
        # For Hybrid mode with controller in local tier, still check global tier constraints
        elif priority_model == "Hybrid Hierarchical":
            # Find all responders used by controllers in the global tier (rank <= depth)
            candidate_responders = []
            for ch in self.app.channel_blocks:
                m = ch.controller_cb.currentText()
                s = ch.responder_cb.currentText()
                if m in nodes and s in nodes:
                    # Check if this controller is in the global tier
                    controller_block = next((b for b in self.app.node_blocks if b.name_edit.text() == m), None)
                    if controller_block and controller_block.controller_hierarchy_rank != -1 and controller_block.controller_hierarchy_rank <= depth_threshold:
                        candidate_responders.append(nodes[s])
            if candidate_responders:
                max_responder_check = max(candidate_responders) # highest responder ID among global tier (lowest priority responder in global tier)

        # --- Build valid responders ---
        valid_responders = []

        # Global hierarchy: controller ID must be strictly > all responders of higher-priority controllers
        if not (is_global and max_responder_check is not None and controller_id < max_responder_check):
            for n in names:
                if n == cur_controller or n not in nodes:
                    continue
                nid = nodes[n]

                # Must be > controller ID
                if nid <= controller_id:
                    continue
                # Must satisfy global hierarchy
                # In compute local hierarchy check we looked at all channels where controller ID < cur_controller ID
                # hence here we don't need to check that the controller of the lowest priority responder is < cur_controller ID
                if max_responder_check is not None and nid < max_responder_check:
                    continue
                valid_responders.append(n)

        # --- Update responder combo ---
        self.responder_cb.clear()
        self.responder_cb.addItems(valid_responders)
        if not valid_responders:
            self.responder_cb.setCurrentIndex(-1)
            self.responder_cb.setPlaceholderText(CHANNEL_NO_RESPONDERS_PLACEHOLDER)
        elif cur_responder in valid_responders:
            self.responder_cb.setCurrentText(cur_responder)
        else:
            self.responder_cb.setCurrentIndex(-1)
            self.responder_cb.setPlaceholderText(CHANNEL_RESPONDER_PLACEHOLDER)

        self.controller_cb.blockSignals(False)
        self.responder_cb.blockSignals(False)

    
    def update_message_options(self, names):
        for cb in (self.lane0_msg_cb, self.lane1_msg_cb):
            current = cb.currentText()
            cb.blockSignals(True)
            cb.clear()
            if names:
                cb.addItems(names)
                if current in names:
                    cb.setCurrentText(current)
                else:
                    cb.setCurrentIndex(-1)
                    cb.setPlaceholderText(CHANNEL_LANE_MESSAGE_PLACEHOLDER)
            else:
                # No messages available
                cb.setCurrentIndex(-1)
                cb.setPlaceholderText(CHANNEL_NO_MESSAGES_PLACEHOLDER)
            cb.blockSignals(False)

    def get_data(self):
        """Return full channel data for saving to JSON"""
        def get_msg_info(cb):
            txt = cb.currentText()
            print("txt", txt)
            # match message name from displayed text
            for d in self.app.descriptors:
                if txt == d["message_name"]:
                    print("d", d)
                    return d  # full dict with message_name/message_descriptor_name/message_size
            return None

        return {
            "name": self.name_edit.text(),
            "global_id": self.global_edit.text(),
            "local_id": self.local_edit.text(),
            "controller": self.controller_cb.currentText(),
            "responder": self.responder_cb.currentText(),
            "lane0_message": get_msg_info(self.lane0_msg_cb),
            "lane0_rx_timeout": self.lane0_to_edit.text(),
            "lane1_message": get_msg_info(self.lane1_msg_cb),
        }

# ---------- Settings Panel ----------
class SettingsDialog(QDialog):
    def __init__(self, parent, settings):
        super().__init__(parent)
        self.setWindowTitle("Settings")
        self.setModal(True)
        self.settings = settings

        layout = QVBoxLayout(self)

        # --- Network Model ---
        layout.addWidget(QLabel("Network Model:"))
        
        self.network_model_cb = QComboBox()
        self.network_model_cb.addItems([
            "32 nodes, 32 local channels",
            "64 nodes, 8 local channels",
            "128 nodes, 2 local channels"
        ])
        self.network_model_cb.setCurrentText(
            settings.get("network_model", "32 nodes, 32 local channels")
        )
        
        network_row = QHBoxLayout()
        network_row.addWidget(self.network_model_cb)
        network_row.addStretch()
        layout.addLayout(network_row)

        # --- Priority model ---
        layout.addWidget(QLabel("Priority Model:"))

        priority_row = QHBoxLayout()
        self.priority_cb = QComboBox()
        self.priority_cb.addItems([
            "Local Hierarchical",
            "Global Hierarchical",
            "Hybrid Hierarchical"
        ])
        self.priority_cb.setCurrentText(
            settings.get("priority_model", "Local Hierarchical")
        )
        priority_row.addWidget(self.priority_cb)

        # Depth field for Hybrid Hierarchical
        depth_label = QLabel("Depth:")
        self.depth_cb = QComboBox()
        max_depth = len(NODE_ID_OPTIONS) // 2 - 1
        self.depth_cb.addItems([str(i) for i in range(max_depth + 1)])
        self.depth_cb.setCurrentText(str(settings.get("priority_model_depth", 0)))
        self.depth_cb.setMaximumWidth(80)
        priority_row.addWidget(depth_label)
        priority_row.addWidget(self.depth_cb)
        priority_row.addStretch()

        # Enable/disable depth field based on priority model
        def update_depth_enabled():
            is_hybrid = self.priority_cb.currentText() == "Hybrid Hierarchical"
            depth_label.setEnabled(is_hybrid)
            self.depth_cb.setEnabled(is_hybrid)
        
        self.priority_cb.currentTextChanged.connect(update_depth_enabled)
        update_depth_enabled()  # Set initial state

        layout.addLayout(priority_row)

        # --- Sync period (controller time-sync emission rate) ---
        layout.addWidget(QLabel("Sync Period:"))
        self.sync_period_spin = QSpinBox()
        self.sync_period_spin.setRange(1, 1000)          # milliseconds (stored as us)
        self.sync_period_spin.setSuffix(" ms")
        self.sync_period_spin.setValue(int(settings.get("sync_period_us", 1000000)) // 1000)
        sync_row = QHBoxLayout()
        sync_row.addWidget(self.sync_period_spin)
        sync_row.addStretch()
        layout.addLayout(sync_row)

        # --- Messages source dropdown ---
        layout.addWidget(QLabel("Messages Source:"))
        self.messages_cb = QComboBox()
        dol_private = Path(__file__).parent / ".." / ".." / ".." / "Core" / "DOL" / "Private"
        dol_private = dol_private.resolve()
        headers = sorted(p.name for p in dol_private.glob("*.h") if p.name != "DOL_Types.h") if dol_private.is_dir() else []
        self.messages_cb.addItem("All", "All")
        for h in headers:
            display = h.removesuffix(".h").replace("_", " ")
            self.messages_cb.addItem(display, h)
        current = settings.get("messages_path", "All")
        idx = self.messages_cb.findData(current)
        self.messages_cb.setCurrentIndex(idx if idx >= 0 else 0)
        messages_row = QHBoxLayout()
        messages_row.addWidget(self.messages_cb)
        messages_row.addStretch()
        layout.addLayout(messages_row)

        # --- Target Language ---
        layout.addWidget(QLabel("Target Language:"))
        
        self.target_language_cb = QComboBox()
        self.target_language_cb.addItems(["C/C++"])
        self.target_language_cb.setCurrentText(
            settings.get("target_language", "C/C++")
        )
        
        language_row = QHBoxLayout()
        language_row.addWidget(self.target_language_cb)
        language_row.addStretch()
        layout.addLayout(language_row)

        # --- Buttons ---
        buttons = QHBoxLayout()
        buttons.addStretch()
        ok = QPushButton("OK")
        cancel = QPushButton("Cancel")
        ok.clicked.connect(self.accept)
        cancel.clicked.connect(self.reject)
        buttons.addWidget(ok)
        buttons.addWidget(cancel)

        layout.addLayout(buttons)

    def get_settings(self):
        return {
            "network_model": self.network_model_cb.currentText(),
            "priority_model": self.priority_cb.currentText(),
            "priority_model_depth": int(self.depth_cb.currentText()) if self.depth_cb.currentText() else 0,
            "sync_period_us": self.sync_period_spin.value() * 1000,
            "messages_path": self.messages_cb.currentData(),
            "target_language": self.target_language_cb.currentText()
        }



# ---------- Main App ----------
class App(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(app_name)
        self.setGeometry(100, 100, 1400, 900)
        self.loading = False
        self.updating_channels = False

        self.current_file_path = None # Path of the currently loaded/saved file
        self.is_dirty = False           # True if unsaved changes exist
        self.installEventFilter(self) # to catch mouse or keyboard events
        

        self.node_blocks = []
        self.channel_blocks = []
        self.settings = {
            "network_model": "32 nodes, 32 local channels",
            "priority_model": "Local Hierarchical",
            "priority_model_depth": 0,
            "sync_period_us": 1000000,
            "messages_path": "All",
            "target_language": "C/C++"
            
        }
        self.descriptors = [] # Message descriptors loaded from file in DOL
        self.selected_edge = None # currently selected edge in graph view
        self.selected_node = None # currently selected node in graph view


        # -------------------- Main widget --------------------
        main_widget = QWidget()
        main_layout = QVBoxLayout()  # vertical: top bar + content
        main_widget.setLayout(main_layout)
        self.setCentralWidget(main_widget)

        # --- Menu Bar ---
        menu_bar = self.menuBar()  # QMainWindow has a built-in menu bar
        menu_bar = self.menuBar()
        menu_bar.setFont(QFont("Segoe UI", 10))

        menu_bar.setStyleSheet(f"""
        QMenuBar {{
            spacing: 0px;                /* distance between top-level items */
            padding: 0px 6px;                /* padding inside the bar */
            font-size: 10pt;
         
        }}

        QMenuBar::item {{
            padding: 4px 8px;            /* top/bottom, left/right inside each menu item */
            margin: 0px;                  /* margin outside each menu item */
            background-color: transparent; /* default background */
        }}

        QMenuBar::item:selected {{
            padding: 4px 8px;            /* maintain padding on selection */             
            background-color: {QColor(self.palette().color(QPalette.Mid)).lighter(110).name()};            
                    
        }}
        """)

        # --- File Menu ---
        file_menu = menu_bar.addMenu("File")

        # ---- New ----
        new_action = QAction("New", self)
        new_action.triggered.connect(self.new_file)
        new_action.setShortcut("Ctrl+N")
        file_menu.addAction(new_action)

        # Save
        save_action = QAction("Save", self)
        save_action.triggered.connect(self.save_data)
        file_menu.addAction(save_action)

        save_as_action = QAction("Save As...", self)
        save_as_action.triggered.connect(self.save_data_as)
        file_menu.addAction(save_as_action)

        # Load
        load_action = QAction("Load...", self)
        load_action.triggered.connect(self.load_data)
        file_menu.addAction(load_action)

        # --- Edit Menu ---
        edit_menu = menu_bar.addMenu("Edit")

        # Settings
        settings_action = QAction("Settings", self)
        settings_action.triggered.connect(self.open_settings)
        edit_menu.addAction(settings_action)

        # Shortcuts
        save_action.setShortcut("Ctrl+S")
        save_as_action.setShortcut("Ctrl+Shift+S")
        load_action.setShortcut("Ctrl+L")
        settings_action.setShortcut("Ctrl+,")

        # -------------------- Top bar --------------------
        top_bar = QHBoxLayout()
        title_color = QColor(Qt.GlobalColor.darkCyan)
        title_label = QLabel(app_name)
        # build stylesheet with dynamic color
        base_bg = QColor(self.palette().color(QPalette.Mid)).lighter(110).name()
        def make_style(col):
            return f"""
        font-weight: bold;
        font-size: 14px;
        color: {col};
        background-color: {base_bg};
        padding: 4px 8px;
        border-radius: 4px;
        """
        title_label.setStyleSheet(make_style(title_color.name()))

        top_bar.addWidget(title_label)
        top_bar.addStretch()  # push buttons to the right


        generate_btn = QPushButton("Generate Configuration")
        
        def make_style(col):
            return f"""
      
        font-size: 12px;
        color: {col};
        background-color: {base_bg};
        padding: 4px 8px;
        border-radius: 4px;
        border: 1px solid {col};
        """
        
        generate_btn.setStyleSheet(make_style(QColor(Qt.GlobalColor.cyan).name()))

        # animate colour fade between darkCyan and cyan, ping‑pong style
        anim = QVariantAnimation(self)
        anim.setDuration(3000)          # full cycle duration
        anim.setLoopCount(-1)           # repeat forever
        anim.setEasingCurve(QEasingCurve.InOutSine)
        # start and end at darkCyan, with cyan in the middle
        anim.setStartValue(QColor(Qt.GlobalColor.darkCyan))
        anim.setEndValue(QColor(Qt.GlobalColor.darkCyan))
        anim.setKeyValueAt(0.5, QColor(Qt.GlobalColor.cyan))
        anim.valueChanged.connect(lambda c: generate_btn.setStyleSheet(make_style(c.name())))
        anim.start()

        generate_btn.clicked.connect(self.generate_network_configuration)
        top_bar.addWidget(generate_btn)

        main_layout.addLayout(top_bar)
        main_layout.addSpacing(8) 

        # -------------------- Separator --------------------
        #separator = QFrame()
        #separator.setFrameShape(QFrame.HLine)
        #separator.setFrameShadow(QFrame.Sunken)
        #main_layout.addWidget(separator)
        #main_layout.addSpacing(8) 
        # -------------------- Main content --------------------
        content_layout = QHBoxLayout()
        main_layout.addLayout(content_layout)

        section_label_style = f"""
        font-weight: bold;
        font-size: 12px;
        background-color: {QColor(self.palette().color(QPalette.Mid)).lighter(110).name()};
        padding: 4px 8px;
        border-radius: 4px;
        """

        # -------------------- Channels --------------------
        left = QVBoxLayout()
        content_layout.addLayout(left)
        channels_label = QLabel("Channels") 
        channels_label.setStyleSheet(section_label_style)
        left.addWidget(channels_label)
        self.channel_scroll = QScrollArea()
        self.channel_container = QWidget()
        self.channel_layout = QVBoxLayout(self.channel_container)
        self.channel_layout.setAlignment(Qt.AlignTop)  # anchor blocks to the top
        self.channel_scroll.setWidgetResizable(True)
        self.channel_scroll.setWidget(self.channel_container)
        left.addWidget(self.channel_scroll)
        left.addWidget(QPushButton("+ Add Channel", clicked=self.add_channel))

        bg_color = QPushButton().palette().color(QPalette.Mid).lighter(110)
        palette = self.channel_container.palette()
        palette.setColor(QPalette.ColorRole.Window, bg_color.name())
        self.channel_container.setAutoFillBackground(True)  # very important!
        self.channel_container.setPalette(palette)

        # -------------------- Nodes --------------------
        middle = QVBoxLayout()
        content_layout.addLayout(middle)
        nodes_label = QLabel("Nodes")
        nodes_label.setStyleSheet(section_label_style)
        middle.addWidget(nodes_label)
        self.node_scroll = QScrollArea()
        self.node_container = QWidget()
        self.node_layout = QVBoxLayout(self.node_container)
        self.node_layout.setAlignment(Qt.AlignTop)  # anchors blocks to top
        self.node_scroll.setWidgetResizable(True)
        self.node_scroll.setWidget(self.node_container)
        middle.addWidget(self.node_scroll)
        middle.addWidget(QPushButton("+ Add Node", clicked=self.add_node))

        palette = self.node_container.palette()
        palette.setColor(QPalette.ColorRole.Window, bg_color.name())
        self.node_container.setAutoFillBackground(True)  # very important!
        self.node_container.setPalette(palette)

        # -------------------- Graph --------------------
        right = QVBoxLayout()
        content_layout.addLayout(right)

        # Graph label top-left inside column
        priority_model = self.settings.get('priority_model','Local Hierarchical')
        if priority_model == "Hybrid Hierarchical":
            depth = self.settings.get('priority_depth', 0)
            model_text = f"Hybrid {depth}-Hierarchical"
        else:
            model_text = priority_model
        self.graph_label = QLabel(f"Network Configuration — {model_text} Model")
        self.graph_label.setStyleSheet(section_label_style)
        right.addWidget(self.graph_label)

        # Graph canvas
        self.scene = QGraphicsScene()
        self.view = GraphView(self.scene, self)
        self.view.setRenderHint(QPainter.Antialiasing)
        right.addWidget(self.view)

        # Limit the width to fit current graph, aligned with other columns
        self.view.setMinimumWidth(400)  # adjust as needed
        #self.view.setMaximumWidth(700)  # optional


        # Graph info box
        self.graph_info = QFrame(self.view)
        self.graph_info.setFixedWidth(280)
        self.graph_info.setStyleSheet(f"""
            QFrame {{
                background-color: #2b2b2b;
                border: 2px solid {QColor(Qt.GlobalColor.cyan).name()};
                border-radius: 6px;
                font-size: 12px;
            }}
        """)

        self.graph_info.ShadowEffect = QGraphicsDropShadowEffect()
        self.graph_info.ShadowEffect.setBlurRadius(15)
        self.graph_info.ShadowEffect.setOffset(3, 3)
        self.graph_info.ShadowEffect.setColor(QColor(0, 0, 0, 160))
        self.graph_info.setGraphicsEffect(self.graph_info.ShadowEffect)


        self.graph_info.hide()

        layout = QVBoxLayout(self.graph_info)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSizeConstraint(QVBoxLayout.SetDefaultConstraint)

        graph_info_title_color = QColor(Qt.GlobalColor.cyan)
        self.graph_info_title = QLabel("Channel Info")
        self.graph_info_title.setStyleSheet(f"font-weight: bold; padding: 0; margin: 0; color: {graph_info_title_color.name()}; border: none; background: transparent;")
        self.graph_info_title.setFixedHeight(20)
   
        self.graph_info_body = QLabel()
        self.graph_info_body.setWordWrap(True)
        self.graph_info_body.setStyleSheet("padding: 0; margin: 0; border: none; background: transparent;")
        self.graph_info_body.setMaximumHeight(600)
   
        layout.addWidget(self.graph_info_title)
        layout.addWidget(self.graph_info_body)



        # --- Set column stretch ---
        content_layout.setStretch(0, 0)  # Channels column does NOT stretch
        content_layout.setStretch(1, 0)  # Nodes column does NOT stretch
        content_layout.setStretch(2, 1)  # Graph column stretches to fill space


        channel_column_width = 380
        node_column_width = 280
        self.channel_scroll.setFixedWidth(channel_column_width)
        self.node_scroll.setFixedWidth(node_column_width)

        # -------------------- Initial Node/Channel --------------------
        self.add_node()
        self.add_channel()
        self.reload_descriptors()
        self.install_dirty_filter(self)

        # --- Refocus main window ---
        QTimer.singleShot(1, lambda: self.setFocus(Qt.OtherFocusReason)) 



    # ---------- Node logic ----------
    def add_node(self):
        block = NodeBlock(len(self.node_blocks)+1, self)
        self.node_layout.addWidget(block); 
        self.node_blocks.append(block)
        last_node = self.node_blocks[-1] # newly added node
        
        last_node.node_id_cb.blockSignals(True)
        last_node.node_id_cb.setCurrentIndex(-1)   # ← THIS is the key
        last_node.node_id_cb.setPlaceholderText(NODE_ID_PLACEHOLDER)
        last_node.node_id_cb.blockSignals(False)

        self.recompute_channels()
        self.update_node_ids()
        # Install dirty filter on the new block and its children
        self.install_dirty_filter(block)

    # --- Focus and scroll to bottom  ---
        def scroll_and_focus():
            # Force layout update
            self.node_container.adjustSize()
            self.node_scroll.verticalScrollBar().setValue(
                self.node_scroll.verticalScrollBar().maximum()
            )
            last_node.name_edit.setFocus()
        QTimer.singleShot(0, scroll_and_focus)


    def delete_node(self, block):
        block.setParent(None); self.node_blocks.remove(block)
        for i,b in enumerate(self.node_blocks,1): b.set_index(i)
        self.recompute_channels()
        self.update_node_ids()

    def compute_node_metrics(self):
        """
        Compute and update node metrics:
        - channel_count: number of channels each node is involved in (as controller or responder)
        - controller_hierarchy_rank: for nodes that are controllers, their rank in the hierarchy (0 = highest)
        - connected_channels: list of channel names this node is connected to
        - controllers: list of controller node names for this node
        """
        nodes = self.get_nodes()
        
        # Initialize all counts to 0, rank to -1, and lists to empty
        for b in self.node_blocks:
            b.listener_endpoints_count = 0
            b.channel_count = 0
            b.controller_hierarchy_rank = -1
            b.connected_channels = []
            b.controllers = []
            b.responders = []
            b.controller_endpoints_count = 0
            b.responder_endpoints_count = 0
        
        # Count channels per node and track connections
        for ch in self.channel_blocks:
            controller_name = ch.controller_cb.currentText()
            responder_name = ch.responder_cb.currentText()
            channel_name = ch.name_edit.text()
            
            # Only count valid, committed channels
            if not controller_name or not responder_name or controller_name not in nodes or responder_name not in nodes:
                continue
            
            # Find the blocks and update their metrics
            for b in self.node_blocks:
                node_name = b.name_edit.text()
                if node_name == controller_name:
                    b.channel_count += 1
                    b.controller_endpoints_count += 1
                    if channel_name:
                        b.connected_channels.append(channel_name)
                    # Track responder for this controller
                    if responder_name not in b.responders:
                        b.responders.append(responder_name)
                if node_name == responder_name:
                    b.channel_count += 1
                    b.responder_endpoints_count += 1
                    if channel_name:
                        b.connected_channels.append(channel_name)
                    # Track controller for this responder
                    if controller_name not in b.controllers:
                        b.controllers.append(controller_name)
        
        # Listener nodes observe every complete channel; they are neither controller nor responder
        complete = [ch for ch in self.channel_blocks
                    if ch.name_edit.text()
                    and ch.controller_cb.currentText() in nodes
                    and ch.responder_cb.currentText() in nodes]
        for b in self.node_blocks:
            if b.is_listener():
                b.connected_channels = [ch.name_edit.text() for ch in complete]
                b.channel_count = len(b.connected_channels)
                b.listener_endpoints_count = b.channel_count
                b.controller_endpoints_count = 0
                b.responder_endpoints_count = 0
                b.controllers = sorted({ch.controller_cb.currentText() for ch in complete})
                b.responders = sorted({ch.responder_cb.currentText() for ch in complete})
        
        # Count controllers with lower ID for each node that is a controller
        for b in self.node_blocks:
            node_name = b.name_edit.text()
            node_id = b.node_id_cb.currentText()
            
            if not node_name or not node_id or node_id not in NODE_ID_OPTIONS:
                b.controller_hierarchy_rank = -1
                continue
            
            node_id_int = int(node_id, 16)
            b.controller_hierarchy_rank = -1  # Default to -1, will be set if this node is a controller
            
            # Check if this node is a controller in any complete channel
            is_controller = any(ch.controller_cb.currentText() == node_name 
                          for ch in self.channel_blocks 
                          if ch.controller_cb.currentText() and ch.responder_cb.currentText()
                          and ch.name_edit.text() and ch.lane0_msg_cb.currentText()
                          and ch.lane0_to_edit.text() and ch.lane1_msg_cb.currentText())
            
            if is_controller:
                # Count unique controllers with lower IDs
                controllers_with_lower_ids = set()
                for ch in self.channel_blocks:
                    controller_name = ch.controller_cb.currentText()
                    if controller_name and controller_name != node_name and controller_name in nodes:
                        controller_id_int = nodes[controller_name]
                        if controller_id_int < node_id_int:
                            controllers_with_lower_ids.add(controller_name)
                b.controller_hierarchy_rank = len(controllers_with_lower_ids)

    def get_nodes(self):
        nodes = {}
        for b in self.node_blocks:
            name, nid = b.name_edit.text(), b.node_id_cb.currentText()
            if name and nid: nodes[name] = int(nid,16)
        return nodes
    
    def endpoint_node_names(self):
        """Nodes that can be controller/responder (listeners excluded)."""
        listeners = {b.name_edit.text() for b in self.node_blocks if b.is_listener()}
        return [n for n in self.get_nodes() if n not in listeners]

    def on_listener_toggled(self, block):
        if self.loading:
            return
        if block.is_listener() and (block.controller_endpoints_count or block.responder_endpoints_count):
            block.listener_cb.blockSignals(True)
            block.listener_cb.setChecked(False)
            block.listener_cb.blockSignals(False)
            QMessageBox.warning(self, "Listener",
                                "This node is controller or responder of a channel. Remove it from those channels first.")
            return
        self.recompute_channels()

    def show_invalid_name_callout(self, line_edit, duration_ms=3500, force=False):
        """Show a context-specific error message for invalid names."""
        validator = line_edit.validator()
        if validator is None:
            return

        text = line_edit.text()
        state, _, _ = validator.validate(text, 0)
        if not force and state == QValidator.Acceptable:
            return
        
        # Determine which validation rule failed
        if text and not text[0].isalpha() and text[0] != '_':
            message = "Name must start with [a-z, A-Z, _]"
        else:
            message = "Name can only contain [a-z, A-Z, 0-9, _]"
        
        self.show_invalid_input_callout(line_edit, message, duration_ms, force=True)

    def show_invalid_input_callout(self, line_edit, message, duration_ms=3500, force=False):
        """Show a temporary callout under the field if its validator rejects the input."""
        validator = line_edit.validator()
        if validator is None:
            return

        text = line_edit.text()
        state, _, _ = validator.validate(text, 0)
        if not force and state == QValidator.Acceptable:
            return

        # Create a temporary callout attached to the nearest block widget
        parent = line_edit
        while parent and not isinstance(parent, (NodeBlock, ChannelBlock)):
            parent = parent.parentWidget()
        if parent is None:
            parent = line_edit.parentWidget()

        callout = ErrorCallout(parent, floating=True)
        field_bg = line_edit.palette().color(QPalette.Base)
        field_text = line_edit.palette().color(QPalette.Text)
        callout.set_colors(field_bg, field_text)
        
        # Get field position in parent
        field_pos_in_parent = line_edit.mapTo(parent, QPoint(0, 0))
        
        # Get the parent layout to find the full row width
        block_layout = parent.layout()
        if block_layout:
            # Use the parent block's content width minus margins
            margins = block_layout.contentsMargins()
            callout_width = parent.width() - margins.left() - margins.right()
            x_offset = margins.left()
        else:
            callout_width = line_edit.width()
            x_offset = field_pos_in_parent.x()
        
        callout.setFixedWidth(callout_width)
        callout.setText(message)

        # Calculate callout height using heightForWidth for accurate measurement
        callout_height = callout.heightForWidth(callout_width)
        
        # Check available space below the field
        field_bottom_global = line_edit.mapToGlobal(QPoint(0, line_edit.height()))
        parent_bottom_global = parent.mapToGlobal(QPoint(0, parent.height()))
        space_below = parent_bottom_global.y() - field_bottom_global.y()
        
        # Position above or below based on available space
        if space_below < callout_height + 8:  # 8px buffer
            # Position above the field
            pos = QPoint(x_offset, field_pos_in_parent.y() - callout_height - 1)
        else:
            # Position below the field
            pos = QPoint(x_offset, field_pos_in_parent.y() + line_edit.height() + 4)
        
        callout.move(pos)
        callout.show()
        callout.raise_()

        # Store original palette only if not already in error state
        # This prevents capturing the red palette when multiple validations occur
        if not hasattr(line_edit, '_original_palette'):
            line_edit._original_palette = line_edit.palette()
        
        # Set palette colors to indicate error
        palette = line_edit.palette()
        palette.setColor(QPalette.Accent, Qt.red)
        palette.setColor(QPalette.Text, Qt.red)
        line_edit.setPalette(palette)

        # Cancel any existing cleanup timer and delete old callout
        if hasattr(line_edit, '_cleanup_timer'):
            line_edit._cleanup_timer.stop()
            line_edit._cleanup_timer.deleteLater()
        if hasattr(line_edit, '_error_callout'):
            line_edit._error_callout.deleteLater()
        
        # Store reference to current callout
        line_edit._error_callout = callout
        
        # Function to cleanup - hide callout and restore palette together
        def cleanup():
            if hasattr(line_edit, '_original_palette'):
                line_edit.setPalette(line_edit._original_palette)
                delattr(line_edit, '_original_palette')
            if hasattr(line_edit, '_cleanup_timer'):
                delattr(line_edit, '_cleanup_timer')
            if hasattr(line_edit, '_error_callout'):
                line_edit._error_callout.deleteLater()
                delattr(line_edit, '_error_callout')
        
        # Hide callout and restore palette after duration
        line_edit._cleanup_timer = QTimer()
        line_edit._cleanup_timer.setSingleShot(True)
        line_edit._cleanup_timer.timeout.connect(cleanup)
        line_edit._cleanup_timer.start(duration_ms)
    

    def validate_unique_names(self):
        """
        Check all node and channel names for uniqueness across both lists.
        Update error callouts and field styling accordingly.
        """
        # Collect all names (node and channel)
        node_names = [b.name_edit.text().strip() for b in self.node_blocks if b.name_edit.text().strip()]
        channel_names = [b.name_edit.text().strip() for b in self.channel_blocks if b.name_edit.text().strip()]
        
        all_names = node_names + channel_names
        
        # Find duplicates
        seen = set()
        duplicates = set()
        for name in all_names:
            if name in seen:
                duplicates.add(name)
            seen.add(name)
        
        # Update node error callouts and borders
        for node_block in self.node_blocks:
            node_name = node_block.name_edit.text().strip()
            if node_name and node_name in duplicates:
                node_block.name_error_label.setText("Name already in use by another node or channel")
                node_block.name_error_label.show()

                palette = node_block.name_edit.palette()
                palette.setColor(QPalette.Accent, Qt.red)
                palette.setColor(QPalette.Text, Qt.red)  
                node_block.name_edit.setPalette(palette)

            else:
                node_block.name_error_label.hide()
                # Restore normal styling
                bg_color = self.view.palette().color(QPalette.Button).lighter(110)
                node_block.name_edit.setStyleSheet(f"QLineEdit {{ background-color: {bg_color.name()}; }}")
        
        # Update channel error callouts and borders
        for channel_block in self.channel_blocks:
            channel_name = channel_block.name_edit.text().strip()
            if channel_name and channel_name in duplicates:
                channel_block.name_error_label.setText("Name already in use by another node or channel")
                channel_block.name_error_label.show()

                palette = channel_block.name_edit.palette()
                palette.setColor(QPalette.Accent, Qt.red)
                palette.setColor(QPalette.Text, Qt.red)  
                channel_block.name_edit.setPalette(palette)

            else:
                channel_block.name_error_label.hide()
                # Restore normal styling
                bg_color = self.view.palette().color(QPalette.Button).lighter(110)
                channel_block.name_edit.setStyleSheet(f"QLineEdit {{ background-color: {bg_color.name()}; }}")
    
    def update_node_ids(self, changed_node=None):
        #Ensure node IDs are unique.
        # 1. Collect selected IDs
        selected_ids = {b.node_id_cb.currentText() for b in self.node_blocks if b.node_id_cb.has_selection()}

        old_index = None
        if changed_node and changed_node in self.node_blocks:
            old_index = self.node_blocks.index(changed_node)

        # 2. Update each combo box
        for b in self.node_blocks:
            current_id = b.node_id_cb.currentText()
            b.node_id_cb.blockSignals(True)  # prevent recursion

            # Clear and rebuild options
            b.node_id_cb.clear()
            # IDs available for this node: all except IDs used by other nodes
            available_ids = [nid for nid in NODE_ID_OPTIONS if nid not in selected_ids or nid == current_id]
            b.node_id_cb.addItems(available_ids)

            # Restore current selection if still valid
            if current_id in available_ids:
                b.node_id_cb.setCurrentText(current_id)
            elif b.node_id_cb.has_selection():
                # optional: pick first available if previous selection became invalid
                # but only if the node had a selection (not just placeholder)
                b.node_id_cb.setCurrentIndex(0)

            b.node_id_cb.blockSignals(False)

        # --- Sort node blocks by ID (empty IDs last) ---
        # Only sort nodes that have all fields filled (name and ID)
        def node_sort_key(b):
            text = b.node_id_cb.currentText()
            name = b.name_edit.text()
            # If node is incomplete (missing name or ID), put it at the end
            if not name or text not in NODE_ID_OPTIONS:
                return (1, int(NODE_ID_OPTIONS[-1], 16) + 1)  # Sort incomplete nodes at end
            else:
                return (0, int(text, 16))  # Sort complete nodes by ID

        self.node_blocks.sort(key=node_sort_key)

        new_index = None
        if changed_node and changed_node in self.node_blocks:
            new_index = self.node_blocks.index(changed_node)

        # --- Rebuild layout in sorted order ---
        for i, b in enumerate(self.node_blocks, 1):
            self.node_layout.removeWidget(b)
            self.node_layout.addWidget(b)
          #  b.set_index(int(b.node_id_cb.currentText(), 16) if b.node_id_cb.has_selection() else None)
            b.set_index(i)
        
        # --- Refocus on the changed node's name field ---
        if changed_node:
            def scroll_and_focus():
                # Check if node is complete (has name and valid ID)
                name = changed_node.name_edit.text()
                text = changed_node.node_id_cb.currentText()
                is_complete = name and text in NODE_ID_OPTIONS
                flash_block_border(changed_node, flashes=2 if is_complete else 1)
                if old_index is not None and new_index is not None and old_index != new_index:
                    self.node_scroll.ensureWidgetVisible(changed_node)
               #♣ changed_node.name_edit.setFocus()
            
            # Schedule after layout update
            QTimer.singleShot(0, scroll_and_focus)



    # ---------- Channel logic ----------
    def add_channel(self):
        block = ChannelBlock(len(self.channel_blocks)+1, self)
        self.channel_layout.addWidget(block); self.channel_blocks.append(block)
        self.recompute_channels()
        self.update_message_lists()  
        # Install dirty filter on the new block and its children
        self.install_dirty_filter(block)

        # --- Focus and scroll into view ---
        def scroll_and_focus():
            self.channel_container.adjustSize()
            self.channel_scroll.verticalScrollBar().setValue(
                self.channel_scroll.verticalScrollBar().maximum()
            )
            block.name_edit.setFocus()

        QTimer.singleShot(0, scroll_and_focus)

    def delete_channel(self, block):
        block.setParent(None); self.channel_blocks.remove(block)
        for i,b in enumerate(self.channel_blocks,1): b.set_index(i)
        self.recompute_channels()

    # ---------- Recompute ----------
    def recompute_channels(self, changed_channel=None):

        if self.loading or self.updating_channels:
            return
        self.updating_channels = True

        nodes = self.get_nodes()
        counters = {}
        network_model = self.settings.get("network_model", "32 nodes, 32 local channels")
        node_bits = max(1, math.ceil(math.log2(extract_network_model_nodes(network_model))))
        local_bits = max(1, math.ceil(math.log2(extract_network_model_local_channels(network_model))))
        print(node_bits, local_bits)

        endpoint_names = self.endpoint_node_names()
        for ch in self.channel_blocks:
            ch.update_nodes(endpoint_names)

        for ch in self.channel_blocks:
            m,s = ch.controller_cb.currentText(), ch.responder_cb.currentText()
            if not m or not s or m not in nodes or s not in nodes or m==s:
                ch.local_edit.setText(""); 
                ch.global_edit.setText(""); 
 
            if m and s and m in nodes and s in nodes:
                controller_id = nodes[m]
                responder_id  = nodes[s]

                if controller_id >= responder_id:
                    ch.controller_cb.blockSignals(True)
                    ch.responder_cb.blockSignals(True)
                    ch.controller_cb.setCurrentIndex(-1)
                    ch.responder_cb.setCurrentIndex(-1)
                    ch.controller_cb.setPlaceholderText(CHANNEL_CONTROLLER_PLACEHOLDER)
                    ch.responder_cb.setPlaceholderText(CHANNEL_RESPONDER_PLACEHOLDER)
                    ch.controller_cb.blockSignals(False)
                    ch.responder_cb.blockSignals(False)

                    ch.local_edit.setText("")
                    ch.global_edit.setText("")
                else:
                    key = tuple(sorted((m,s))); local = counters.get(key,0); counters[key]=local+1
                    lower,higher = min(nodes[m],nodes[s]), max(nodes[m],nodes[s])
                    global_id = (lower<<(node_bits + local_bits))|(higher<<local_bits)|local
                    ch.local_edit.setText(str(local))
                    ch.global_edit.setText(f"0x{global_id:04X}")


        # --- Step 3: Apply Global Hierarchy (if selected) ---
        priority_model = self.settings.get("priority_model")
        if priority_model == "Global Hierarchical" or priority_model == "Hybrid Hierarchical":
            # Build list of all committed channels
            committed_channels = []
            for ch in self.channel_blocks:
                m, s = ch.controller_cb.currentText(), ch.responder_cb.currentText()
                if not m or not s or m not in nodes or s not in nodes:
                    continue
                mid, sid = nodes[m], nodes[s]
                if ch.local_edit.text() != "" and ch.global_edit.text() != "":
                    # For Hybrid mode, only include channels where controller rank <= depth
                    if priority_model == "Hybrid Hierarchical":
                        controller_block = next((b for b in self.node_blocks if b.name_edit.text() == m), None)
                        if controller_block:
                            depth_threshold = self.settings.get("priority_model_depth", 0)
                            if controller_block.controller_hierarchy_rank != -1 and controller_block.controller_hierarchy_rank <= depth_threshold:
                                committed_channels.append((mid, sid))
                    else:
                        committed_channels.append((mid, sid))

            # Find lowest-priority responder among committed channels
            if committed_channels:
                # lowest-priority responder = max ID (higher ID = lower priority)
                lowest_responder_id = max(sid for _, sid in committed_channels)
               
                # find the controller of that responder
                controller_of_lowest = next(mid for mid, sid in committed_channels if sid == lowest_responder_id)
              
            else:
                lowest_responder_id = None
                controller_of_lowest = None

            # Now check each channel
            for ch in self.channel_blocks:
                m, s = ch.controller_cb.currentText(), ch.responder_cb.currentText()
                if not m or not s or m not in nodes or s not in nodes:
                    continue
                mid, sid = nodes[m], nodes[s]
  
                # Always enforce controller < responder
                if mid >= sid:
                    ch.controller_cb.blockSignals(True)
                    ch.responder_cb.blockSignals(True)
                    ch.controller_cb.setCurrentIndex(-1)
                    ch.responder_cb.setCurrentIndex(-1)
                    ch.controller_cb.setPlaceholderText(CHANNEL_CONTROLLER_PLACEHOLDER)
                    ch.responder_cb.setPlaceholderText(CHANNEL_RESPONDER_PLACEHOLDER)
                    ch.controller_cb.blockSignals(False)
                    ch.responder_cb.blockSignals(False)
                    ch.local_edit.setText("")
                    ch.global_edit.setText("")
                    continue

                # --- Global Hierarchy check ---
                if lowest_responder_id is not None:
                    # Controller ID must be > all responders of higher-priority controllers,
                    # and responder must not sit below a higher-priority controller's responder range
                    higher_resp = [sid2 for mid2, sid2 in committed_channels if mid2 < mid]
                    max_higher_resp = max(higher_resp) if higher_resp else None
                    if (max_higher_resp is not None and mid < max_higher_resp) or \
                       (controller_of_lowest < mid and lowest_responder_id > sid):
                        # This channel violates the rule: abort
                        ch.controller_cb.blockSignals(True)
                        ch.responder_cb.blockSignals(True)
                        ch.controller_cb.setCurrentIndex(-1)
                        ch.responder_cb.setCurrentIndex(-1)
                        ch.controller_cb.setPlaceholderText(CHANNEL_CONTROLLER_PLACEHOLDER)
                        ch.responder_cb.setPlaceholderText(CHANNEL_RESPONDER_PLACEHOLDER)
                        ch.controller_cb.blockSignals(False)
                        ch.responder_cb.blockSignals(False)
                        ch.local_edit.setText("")
                        ch.global_edit.setText("")
        
        # Sort channels by decimal global ID, but only if all fields are filled
        def global_id_decimal(ch):
            # Check if all required fields are filled
            has_all_fields = (ch.name_edit.text() and 
                            ch.controller_cb.currentText() and 
                            ch.responder_cb.currentText() and
                            ch.lane0_msg_cb.currentText() and
                            ch.lane0_to_edit.text() and
                            ch.lane1_msg_cb.currentText())
            
            text = ch.global_edit.text()
            if not has_all_fields:
                return (1, float('inf'))  # Sort incomplete channels at end
            elif text.startswith("0x"):
                return (0, int(text, 16))
            elif text.isdigit():
                return (0, int(text))
            else:
                return (1, float('inf'))  # invalid IDs go last

        old_index = None
        if changed_channel and changed_channel in self.channel_blocks:
            old_index = self.channel_blocks.index(changed_channel)

        self.channel_blocks.sort(key=global_id_decimal)

        new_index = None
        if changed_channel and changed_channel in self.channel_blocks:
            new_index = self.channel_blocks.index(changed_channel)

        for i, ch in enumerate(self.channel_blocks, 1):
            self.channel_layout.removeWidget(ch)
            self.channel_layout.addWidget(ch)
            ch.set_index(i)

            """
            gid_text = ch.global_edit.text()
            if gid_text.startswith("0x"):
                dec_id = int(gid_text, 16)
                ch.set_index(dec_id)
            elif gid_text.isdigit():
                ch.set_index(int(gid_text))
            else:
                ch.set_index(None)
            """
        # Scroll/focus the channel that just changed
        if changed_channel:
            def scroll_and_focus():
                # Check if channel is complete (all 7 fields filled)
                # Only check if changed_channel is actually a ChannelBlock
                is_complete = False
                if hasattr(changed_channel, 'controller_cb'):  # It's a ChannelBlock
                    is_complete = (changed_channel.name_edit.text() and 
                                 changed_channel.controller_cb.currentText() and 
                                 changed_channel.responder_cb.currentText() and
                                 changed_channel.lane0_msg_cb.currentText() and
                                 changed_channel.lane0_to_edit.text() and
                                 changed_channel.lane1_msg_cb.currentText())
                flash_block_border(changed_channel, flashes=2 if is_complete else 1)
                if old_index is not None and new_index is not None and old_index != new_index:
                    self.channel_scroll.ensureWidgetVisible(changed_channel)
            QTimer.singleShot(0, scroll_and_focus)

        # Compute and update node metrics
        self.compute_node_metrics()

        self.update_graph()
        self.updating_channels = False

    # ---------- Graph ----------
    def update_graph(self):
        self.selected_edge = None
        self.selected_node = None
        self.graph_info.hide()
        self.scene.clear()
        nodes = self.get_nodes()
        # Only include channels where ALL fields are filled
        channels = [
            (c.controller_cb.currentText(), c.responder_cb.currentText(), c) 
            for c in self.channel_blocks 
            if (c.name_edit.text() and 
                c.controller_cb.currentText() and 
                c.responder_cb.currentText() and
                c.lane0_msg_cb.currentText() and
                c.lane0_to_edit.text() and
                c.lane1_msg_cb.currentText())
        ]

        # Get viewport size for initial calculation
        cw, ch = max(self.view.viewport().width(), 1), max(self.view.viewport().height(), 1)

        # --- Build tree structure from controller-responder relationships ---
        responders_of = {}   # controller -> list of responders (unique)
        controllers_of = {}  # responder -> list of controllers (unique)
        
        for m, s, _ in channels:
            if m in nodes and s in nodes and m != s:
                if m not in responders_of:
                    responders_of[m] = []
                if s not in responders_of[m]:  # Only add if not already present
                    responders_of[m].append(s)
                if s not in controllers_of:
                    controllers_of[s] = []
                if m not in controllers_of[s]:  # Only add if not already present
                    controllers_of[s].append(m)
        
        # --- Assign vertical levels based on controller hierarchy rank and relationships ---
        node_positions = {}
        node_level = {}
        level_nodes = {}
        
        # Get all nodes with their controller hierarchy rank
        node_rank = {}
        for node in nodes:
            node_block = next((b for b in self.node_blocks if b.name_edit.text() == node), None)
            if node_block:
                node_rank[node] = node_block.controller_hierarchy_rank
        
        # Layout: controller -> its responders -> next controller -> its responders, etc.
        # If a lower priority controller shares a responder, connection goes backward
        node_level = {}
        level_nodes = {}
        
        # Find connected nodes (nodes involved in any channel)
        connected_nodes = set(responders_of.keys()) | set(controllers_of.keys())
        isolated_nodes = [n for n in nodes if n not in connected_nodes]
        
        current_level = 0
        
        all_ranks = sorted(set(node_rank.values()))
        
        # Process each controller rank in order
        for rank in all_ranks:
            if rank == -1:
                # Non-controllers go at the end
                continue
            
            # Get controllers at this rank (sorted by ID)
            controllers_at_rank = sorted(
                [n for n in nodes if node_rank.get(n) == rank and n in responders_of and n in connected_nodes],
                key=lambda n: nodes[n]
            )
            
            # Place each controller and its responders
            for controller in controllers_at_rank:
                # Only place controller if not already placed as a responder
                if controller not in node_level:
                    node_level[controller] = current_level
                    if current_level not in level_nodes:
                        level_nodes[current_level] = []
                    level_nodes[current_level].append(controller)
                    current_level += 1
                
                # Place responders of this controller at next level (if not already placed)
                if controller in responders_of:
                    responders_to_place = [s for s in responders_of[controller] if s not in node_level]
                    if responders_to_place:
                        for responder in responders_to_place:
                            node_level[responder] = current_level
                            if current_level not in level_nodes:
                                level_nodes[current_level] = []
                            level_nodes[current_level].append(responder)
                        current_level += 1
        
        # Place non-controllers (rank -1) that haven't been placed yet
        non_controllers = [n for n in nodes if node_rank.get(n) == -1 and n in connected_nodes and n not in node_level]
        if non_controllers:
            for node in non_controllers:
                if node not in node_level:
                    node_level[node] = current_level
                    if current_level not in level_nodes:
                        level_nodes[current_level] = []
                    level_nodes[current_level].append(node)
            current_level += 1
        
        # Place isolated nodes at the very end
        if isolated_nodes:
            for node in isolated_nodes:
                node_level[node] = current_level
                if current_level not in level_nodes:
                    level_nodes[current_level] = []
                level_nodes[current_level].append(node)

        # Calculate positions - Two pass approach
        # Pass 1: Position all nodes in a simple grid from top
        level_spacing = 100  # horizontal spacing between levels
        node_spacing = 50    # vertical spacing between nodes in same level
        temp_positions = {}
        
        for level in sorted(level_nodes.keys()):
            level_node_list = level_nodes[level]
            # Sort nodes within level by controller rank, then by ID
            level_node_list.sort(key=lambda n: (
                node_rank.get(n, 999),
                nodes[n]
            ))
            
            start_y = NODE_RADIUS + 50
            x = NODE_RADIUS + 50 + level * level_spacing
            
            for i, node in enumerate(level_node_list):
                y = start_y + i * node_spacing
                temp_positions[node] = QPointF(x, y)
        
        # Pass 2: Center controllers relative to their responders
        node_positions = {}
        for node in nodes:
            if node not in temp_positions:
                continue
                
            # If this node is a controller with responders, center it relative to its responders
            # BUT exclude responders at the same level to avoid overlap
            if node in responders_of:
                # Only consider responders that are NOT at the same level as this controller
                responders_not_same_level = [s for s in responders_of[node] 
                                         if s in temp_positions and node_level.get(s, -1) != node_level.get(node, -1)]
                if responders_not_same_level:
                    # Check if there are other controllers at the same level with the same responders
                    # If so, skip centering to maintain even spacing
                    same_level_controllers = level_nodes.get(node_level.get(node, -1), [])
                    siblings_with_same_responders = 0
                    for other_controller in same_level_controllers:
                        if other_controller != node and other_controller in responders_of:
                            other_responders = [s for s in responders_of[other_controller]
                                          if s in temp_positions and node_level.get(s, -1) != node_level.get(other_controller, -1)]
                            # Check if they share any responders
                            if any(s in responders_not_same_level for s in other_responders):
                                siblings_with_same_responders += 1
                    
                    # Only center if this controller doesn't have siblings going to the same responders
                    if siblings_with_same_responders == 0:
                        responder_positions = [temp_positions[s] for s in responders_not_same_level]
                        # Calculate center Y of responders
                        avg_y = sum(p.y() for p in responder_positions) / len(responder_positions)
                        
                        # Check if centering would cause overlap with other nodes at same level
                        would_overlap = False
                        for other_node in same_level_controllers:
                            if other_node != node and other_node in temp_positions:
                                other_y = temp_positions[other_node].y()
                                # Check if new position would be too close (within node_spacing)
                                if abs(avg_y - other_y) < node_spacing * 0.8:
                                    would_overlap = True
                                    break
                        
                        if not would_overlap:
                            node_positions[node] = QPointF(temp_positions[node].x(), avg_y)
                        else:
                            node_positions[node] = temp_positions[node]
                    else:
                        node_positions[node] = temp_positions[node]
                else:
                    node_positions[node] = temp_positions[node]
            else:
                node_positions[node] = temp_positions[node]
        
        # Pass 3: Center responder groups relative to their controllers
        # Group responders at the same level that share the same set of controllers
        processed = set()
        for node in nodes:
            if node not in node_positions or node in processed:
                continue
            if node not in controllers_of:
                continue
            
            controllers_not_same_level = [m for m in controllers_of[node]
                                         if m in node_positions and node_level.get(m, -1) != node_level.get(node, -1)]
            if not controllers_not_same_level:
                continue
            
            # Find sibling responders at the same level with overlapping controllers
            my_level = node_level.get(node, -1)
            same_level_nodes = [n for n in level_nodes.get(my_level, []) if n in node_positions]
            
            # Build group: all responders at this level whose controllers overlap with this node's controllers
            group = [node]
            for other in same_level_nodes:
                if other != node and other in controllers_of:
                    other_ctrls = [m for m in controllers_of[other]
                                  if m in node_positions and node_level.get(m, -1) != my_level]
                    if any(m in controllers_not_same_level for m in other_ctrls):
                        group.append(other)
            
            # Collect all controllers for the group
            all_group_controllers = set()
            for g in group:
                for m in controllers_of.get(g, []):
                    if m in node_positions and node_level.get(m, -1) != my_level:
                        all_group_controllers.add(m)
            
            if not all_group_controllers:
                continue
            
            # Center the group around the average Y of their controllers
            avg_ctrl_y = sum(node_positions[m].y() for m in all_group_controllers) / len(all_group_controllers)
            
            # Sort group by current Y to maintain relative order
            group.sort(key=lambda n: node_positions[n].y())
            group_center = sum(node_positions[n].y() for n in group) / len(group)
            shift = avg_ctrl_y - group_center
            
            for n in group:
                old = node_positions[n]
                node_positions[n] = QPointF(old.x(), old.y() + shift)
                processed.add(n)

        # Pass 4: Enforce minimum vertical spacing between nodes at the same level
        min_node_gap = 2 * NODE_RADIUS + 30
        for level in level_nodes:
            seen = set()
            unique = []
            for n in level_nodes[level]:
                if n not in seen and n in node_positions:
                    seen.add(n)
                    unique.append(n)
            unique.sort(key=lambda n: node_positions[n].y())
            for i in range(1, len(unique)):
                prev_y = node_positions[unique[i-1]].y()
                curr_y = node_positions[unique[i]].y()
                if curr_y - prev_y < min_node_gap:
                    node_positions[unique[i]] = QPointF(
                        node_positions[unique[i]].x(),
                        prev_y + min_node_gap
                    )

        # Pass 5: Re-center controllers on their responders
        for node in nodes:
            if node not in node_positions or node not in responders_of:
                continue
            resps = [s for s in responders_of[node]
                     if s in node_positions and node_level.get(s, -1) != node_level.get(node, -1)]
            if resps:
                avg_y = sum(node_positions[s].y() for s in resps) / len(resps)
                node_positions[node] = QPointF(node_positions[node].x(), avg_y)

        # Re-enforce minimum spacing after re-centering
        for level in level_nodes:
            seen = set()
            unique = []
            for n in level_nodes[level]:
                if n not in seen and n in node_positions:
                    seen.add(n)
                    unique.append(n)
            unique.sort(key=lambda n: node_positions[n].y())
            for i in range(1, len(unique)):
                prev_y = node_positions[unique[i-1]].y()
                curr_y = node_positions[unique[i]].y()
                if curr_y - prev_y < min_node_gap:
                    node_positions[unique[i]] = QPointF(
                        node_positions[unique[i]].x(),
                        prev_y + min_node_gap
                    )

        # Pass 6: Re-center responder groups on their controllers (after controllers moved)
        processed = set()
        for node in nodes:
            if node not in node_positions or node in processed:
                continue
            if node not in controllers_of:
                continue
            controllers_not_same_level = [m for m in controllers_of[node]
                                         if m in node_positions and node_level.get(m, -1) != node_level.get(node, -1)]
            if not controllers_not_same_level:
                continue
            my_level = node_level.get(node, -1)
            same_level_nodes = [n for n in level_nodes.get(my_level, []) if n in node_positions]
            group = [node]
            for other in same_level_nodes:
                if other != node and other in controllers_of:
                    other_ctrls = [m for m in controllers_of[other]
                                  if m in node_positions and node_level.get(m, -1) != my_level]
                    if any(m in controllers_not_same_level for m in other_ctrls):
                        group.append(other)
            all_group_controllers = set()
            for g in group:
                for m in controllers_of.get(g, []):
                    if m in node_positions and node_level.get(m, -1) != my_level:
                        all_group_controllers.add(m)
            if not all_group_controllers:
                continue
            avg_ctrl_y = sum(node_positions[m].y() for m in all_group_controllers) / len(all_group_controllers)
            group.sort(key=lambda n: node_positions[n].y())
            group_center = sum(node_positions[n].y() for n in group) / len(group)
            shift = avg_ctrl_y - group_center
            for n in group:
                old = node_positions[n]
                node_positions[n] = QPointF(old.x(), old.y() + shift)
                processed.add(n)

        # Final spacing enforcement
        for level in level_nodes:
            seen = set()
            unique = []
            for n in level_nodes[level]:
                if n not in seen and n in node_positions:
                    seen.add(n)
                    unique.append(n)
            unique.sort(key=lambda n: node_positions[n].y())
            for i in range(1, len(unique)):
                prev_y = node_positions[unique[i-1]].y()
                curr_y = node_positions[unique[i]].y()
                if curr_y - prev_y < min_node_gap:
                    node_positions[unique[i]] = QPointF(
                        node_positions[unique[i]].x(),
                        prev_y + min_node_gap
                    )

        # Calculate actual bounds needed for all nodes
        if node_positions:
            # Calculate graph dimensions without margins
            base_min_x = min(pos.x() for pos in node_positions.values())
            base_max_x = max(pos.x() for pos in node_positions.values())
            base_min_y = min(pos.y() for pos in node_positions.values())
            base_max_y = max(pos.y() for pos in node_positions.values())
            
            # Add margins for visual spacing
            margin_left = NODE_RADIUS + 50
            margin_right = NODE_RADIUS + 100
            margin_top = NODE_RADIUS + 50
            margin_bottom = NODE_RADIUS + 50
            
            graph_width = (base_max_x - base_min_x) + margin_left + margin_right
            graph_height = (base_max_y - base_min_y) + margin_top + margin_bottom
            
            # Center the graph in the viewport
            offset_x = max(0, (cw - graph_width) / 2) + margin_left - base_min_x
            offset_y = max(0, (ch - graph_height) / 2) + margin_top - base_min_y
            
            # Apply centering offset to all positions
            for node in node_positions:
                node_positions[node] = QPointF(
                    node_positions[node].x() + offset_x,
                    node_positions[node].y() + offset_y
                )
            
            # Set scene rect to fit graph (minimum is viewport size)
            scene_width = max(graph_width, cw)
            scene_height = max(graph_height, ch)
            self.scene.setSceneRect(0, 0, scene_width, scene_height)
        else:
            self.scene.setSceneRect(0, 0, cw, ch)

        # --- Draw nodes ---
        for node, pos in node_positions.items():
            # Find the node block for this node
            node_block = next((b for b in self.node_blocks if b.name_edit.text() == node), None)
            
            # Add text items first
            id_text = self.scene.addText(f"0x{nodes[node]:02X}")
            id_text.setDefaultTextColor(Qt.GlobalColor.darkCyan)
            id_text.setFont(QFont("Consolas", 10, QFont.Bold))
            id_text.setAcceptHoverEvents(False)
            id_text.setPos(pos.x() - id_text.boundingRect().width()/2, pos.y() - id_text.boundingRect().height()/2)
            id_text.setZValue(1)
            
            name_text = self.scene.addText(node)
            name_text.setFont(QFont("Consolas", 10))
            name_text.setAcceptHoverEvents(False)
            name_text.setPos(pos.x() - name_text.boundingRect().width()/2, pos.y() + NODE_RADIUS + 4)
            name_text.setZValue(1)
            
            # Create interactive node graphic and pass text items to it
            node_graphic = NodeGraphic(node_block, self, text_items=[id_text, name_text])
            node_graphic.setPos(pos)
            self.scene.addItem(node_graphic)

        # --- Draw edges ---
        pair_counts = {}
        for m, s, _ in channels:
            if m in nodes and s in nodes and m != s:
                key = (m, s)
                pair_counts[key] = pair_counts.get(key, 0) + 1

        pair_index = {}

        for m, s, ch in channels:
            if m not in node_positions or s not in node_positions or m == s:
                continue

            start_center = node_positions[m]
            end_center = node_positions[s]

            key = (m, s)
            idx = pair_index.get(key, 0)
            total = pair_counts[key]
            pair_index[key] = idx + 1
            offset = (idx - (total-1)/2) * 6

            # --- Connection points ---
            if start_center.x() == end_center.x():
                start_pos = QPointF(start_center.x() + NODE_RADIUS, start_center.y() + offset)
                end_pos   = QPointF(end_center.x() + NODE_RADIUS, end_center.y() + offset)
                cp1 = QPointF(start_pos.x() + 40, start_pos.y())
                cp2 = QPointF(end_pos.x() + 40, end_pos.y())
            elif start_center.x() < end_center.x():
                start_pos = QPointF(start_center.x() + NODE_RADIUS, start_center.y() + offset)
                end_pos   = QPointF(end_center.x() - NODE_RADIUS, end_center.y() + offset)
                mid_x = (start_pos.x() + end_pos.x()) / 2
                cp1 = QPointF(mid_x, start_pos.y())
                cp2 = QPointF(mid_x, end_pos.y())
            else:
                start_pos = QPointF(start_center.x() - NODE_RADIUS, start_center.y() + offset)
                end_pos   = QPointF(end_center.x() + NODE_RADIUS, end_center.y() + offset)
                mid_x = (start_pos.x() + end_pos.x()) / 2
                cp1 = QPointF(mid_x, start_pos.y())
                cp2 = QPointF(mid_x, end_pos.y())

            # --- Create edge with arrowhead inside ---
            path = QPainterPath(start_pos)
            path.cubicTo(cp1, cp2, end_pos)
            edge = ChannelEdge(path, channel=ch, app=self)
            self.scene.addItem(edge)


    def select_edge(self, edge):
        if self.selected_edge:
            self.selected_edge.set_selected(False)
        if self.selected_node:
            self.selected_node.set_selected(False)
            self.selected_node = None

        self.selected_edge = edge
        edge.set_selected(True)
        self.show_channel_info(edge.channel)
        
        # Scroll to and flash the channel block in the list
        def scroll_and_flash():
            self.channel_scroll.ensureWidgetVisible(edge.channel)
            flash_block_border(edge.channel, flashes=2)
        QTimer.singleShot(0, scroll_and_flash)

    def select_node(self, node_block):
        if self.selected_node:
            self.selected_node.set_selected(False)
        if self.selected_edge:
            self.selected_edge.set_selected(False)
            self.selected_edge = None

        # Find the NodeGraphic for this node_block
        node_graphics = [item for item in self.scene.items() if isinstance(item, NodeGraphic) and item.node_block == node_block]
        if node_graphics:
            self.selected_node = node_graphics[0]
            self.selected_node.set_selected(True)
            self.show_node_info(node_block)
            
            # Scroll to and flash the node block in the list
            def scroll_and_flash():
                self.node_scroll.ensureWidgetVisible(node_block)
                flash_block_border(node_block, flashes=2)
            QTimer.singleShot(0, scroll_and_flash)

    def show_channel_info(self, channel):
        value_color = QColor(Qt.GlobalColor.darkGray)
        self.graph_info_title.setText("Channel Info")
        self.graph_info_body.setText(
            f"""
    <b>Name:</b> <span style="color: {value_color.name()}">{channel.name_edit.text()}</span><br>
    <b>Controller:</b> {channel.controller_cb.currentText()}<br>
    <b>Responder:</b> {channel.responder_cb.currentText()}<br><br>
    <b>Role:</b> {"Listener" if data.get("is_listener") else "Node"}<br>

    <b>Global ID:</b> {channel.global_edit.text()}<br>
    <b>Local ID:</b> {channel.local_edit.text()}<br><br>

    <b>Lane 0:</b> {channel.lane0_msg_cb.currentText()}<br>
    <b>Timeout:</b> {channel.lane0_to_edit.text()}<br><br>

    <b>Lane 1:</b> {channel.lane1_msg_cb.currentText()}<br>
    
    """
        )
        # Force label to calculate proper height with word wrap
        self.graph_info_body.setMinimumHeight(0)  # Reset
        self.graph_info_body.setFixedWidth(264)  # 280 - 16 (margins)
        needed_height = self.graph_info_body.heightForWidth(264)
        if needed_height > 0:
            self.graph_info_body.setMinimumHeight(needed_height)
        # Adjust height to fit content (width is fixed)
        self.graph_info.adjustSize()
        # Position relative to view
        viewport_rect = self.view.viewport().rect()
        self.graph_info.move(viewport_rect.width() - self.graph_info.width() - 10, 10)
        self.graph_info.show()

    def show_node_info(self, node_block):
        """Display node information in the graph info panel."""
        self.graph_info_title.setText("Node Info")
        data = node_block.get_data()
        node_id = data.get("node_id", "—")
        channel_count = data.get("channel_count", 0)
        controller_hierarchy_rank = data.get("controller_hierarchy_rank", -1)
        connected_channels = data.get("connected_channels", [])
        controllers = data.get("controllers", [])
        
        # Display rank as "None" if -1 (not a controller)
        rank_display = "None" if controller_hierarchy_rank == -1 else controller_hierarchy_rank
        
        # Format connected channels
        channels_display = ", ".join(connected_channels) if connected_channels else "None"
        
        # Format controllers
        controllers_display = ", ".join(controllers) if controllers else "None"
        
        # Format responders
        responders = data.get("responders", [])
        responders_display = ", ".join(responders) if responders else "None"
        
        self.graph_info_body.setText(
            f"""
    <b>Node Name:</b> {data.get("name", "—")}<br>
    <b>Node ID:</b> {node_id}<br><br>

    <b>Channels Connected:</b> {channel_count}<br>
    <b>Channel Names:</b> {channels_display}<br><br>
    
    <b>Controllers:</b> {controllers_display}<br>
    <b>Controller Hierarchy Rank:</b> {rank_display}<br>
    <b>Responders:</b> {responders_display}
    """
        )
        # Force label to calculate proper height with word wrap
        self.graph_info_body.setMinimumHeight(0)  # Reset
        self.graph_info_body.setFixedWidth(264)  # 280 - 16 (margins)
        needed_height = self.graph_info_body.heightForWidth(264)
        if needed_height > 0:
            self.graph_info_body.setMinimumHeight(needed_height)
        # Adjust height to fit content (width is fixed)
        self.graph_info.adjustSize()
        # Position relative to view
        viewport_rect = self.view.viewport().rect()
        self.graph_info.move(viewport_rect.width() - self.graph_info.width() - 10, 10)
        self.graph_info.show()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.graph_info.move(
            self.view.width() - self.graph_info.width() - 10,
            10
        )


    # ---------- Settings Panel ----------
    def open_settings(self):
        dlg = SettingsDialog(self, self.settings)
        if dlg.exec():
            old_network_model = self.settings.get("network_model", "32 nodes, 32 local channels")
            old_priority_model = self.settings.get("priority_model", "Local Hierarchical")
            old_priority_depth = self.settings.get("priority_model_depth", 0)
            old_messages_path = self.settings.get("messages_path", "")
            # Replace the entire settings dict to preserve key order from get_settings()
            self.settings = dlg.get_settings()
            new_network_model = self.settings.get("network_model", "32 nodes, 32 local channels")
            new_priority_model = self.settings.get("priority_model", "Local Hierarchical")
            new_priority_depth = self.settings.get("priority_model_depth", 0)
            new_messages_path = self.settings.get("messages_path", "")
            
            self.is_dirty = True
            self.setWindowTitle(f"{app_name} *")
            if old_messages_path != new_messages_path:
                self.reload_descriptors()
            
            network_changed = old_network_model != new_network_model
            priority_changed = old_priority_model != new_priority_model
            depth_changed = old_priority_depth != new_priority_depth

            # Refresh node ID options when network model changes.
            if old_network_model != new_network_model:
                self.refresh_node_id_options()
                self.update_graph()

            # Recompute channel IDs and validity whenever settings that affect channel logic change.
            if network_changed or priority_changed or depth_changed:
                self.recompute_channels()
            
            priority_model = self.settings.get('priority_model','Local Hierarchical')
            if priority_model == "Hybrid Hierarchical":
                depth = self.settings.get('priority_model_depth', 0)
                model_text = f"Hybrid {depth}-Hierarchical"
            else:
                model_text = priority_model
            self.graph_label.setText(f"Network Configuration — {model_text} Model")

    def refresh_node_id_options(self):
        """Update all node ID dropdowns when network model changes"""
        global NODE_ID_OPTIONS
        network_model = self.settings.get("network_model", "32 nodes, 32 local channels")
        NODE_ID_OPTIONS = generate_node_id_options(network_model)
        invalidated = False
        
        # Update all node ID comboboxes
        for node_block in self.node_blocks:
            current_value = node_block.node_id_cb.currentText()
            node_block.node_id_cb.blockSignals(True)
            node_block.node_id_cb.clear()
            node_block.node_id_cb.setPlaceholderText(NODE_ID_PLACEHOLDER)
            node_block.node_id_cb.addItems(NODE_ID_OPTIONS)
            # Restore previous value if it's still valid
            if current_value in NODE_ID_OPTIONS:
                node_block.node_id_cb.setCurrentText(current_value)
            else:
                node_block.node_id_cb.setCurrentIndex(-1)
                if current_value:
                    invalidated = True
            node_block.node_id_cb.blockSignals(False)
        # Re-evaluate node IDs for uniqueness and ordering
        self.update_node_ids()
        return invalidated

    # ---------- Message Descriptors ----------
    def reload_descriptors(self):
        self.descriptors.clear()

        selection = self.settings.get("messages_path", "All")
        dol_private = Path(__file__).parent / ".." / ".." / ".." / "Core" / "DOL" / "Private"
        dol_private = dol_private.resolve()
        if not dol_private.is_dir():
            self.update_message_lists()
            return

        if selection == "All":
            headers = [p for p in sorted(dol_private.glob("*.h")) if p.name != "DOL_Types.h"]
        else:
            h = dol_private / selection
            headers = [h] if h.exists() else []

        for path in headers:
            descriptors = parse_descriptors_from_file(str(path))
            size_map = parse_message_sizes_from_c(str(path))
            for d in descriptors:
                d["message_size"] = size_map.get(d["message_descriptor_name"])
            self.descriptors.extend(descriptors)

        self.update_message_lists()

    def update_message_lists(self):
        names = [d["message_name"] for d in self.descriptors]

        for ch in self.channel_blocks:
            ch.update_message_options(names)


    # ---------- New File ----------
    def new_file(self):
        if self.is_dirty:
            # Ask user if they want to save
            reply = QMessageBox.question(
                self,
                "Unsaved Changes",
                "You have unsaved changes. Do you want to save before creating a new configuration?",
                QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel
            )

            if reply == QMessageBox.Cancel:
                return  # do nothing
            elif reply == QMessageBox.Yes:
                self.save_data()
                # if user canceled save dialog, do not proceed
                if self.is_dirty:  # still dirty → user canceled save
                    return
            elif reply == QMessageBox.No:
                self.is_dirty = False
                self.setWindowTitle(app_name)
                pass  # proceed without saving

        # Clear current configuration
        for b in self.node_blocks + self.channel_blocks:
            b.setParent(None)
        self.node_blocks.clear()
        self.channel_blocks.clear()

        # Reset settings and file path
        self.settings = {
            "network_model": "32 nodes, 32 local channels",
            "priority_model": "Local Hierarchical",
            "priority_model_depth": 0,
            "sync_period_us": 1000000,
            "messages_path": "All",
            "target_language": "C/C++"
        }
        self.current_file_path = None  # Clear file path for new config
        self.is_dirty = False

        # Add initial default node/channel
        self.add_node()
        self.add_channel()


    # ---------- Changes Tracking ----------
    def eventFilter(self, obj, event):
        # Any mouse click or key press inside the window marks it dirty
        if event.type() in (QEvent.MouseButtonPress, QEvent.KeyPress):
            self.mark_dirty()
        return super().eventFilter(obj, event)
    
    def install_dirty_filter(self, widget):
        """Recursively install the event filter on widget and all its children."""
        widget.installEventFilter(self)
        for child in widget.findChildren(QWidget):
            child.installEventFilter(self)
    
    def mark_dirty(self):
        if not self.is_dirty:
            self.is_dirty = True
            self.setWindowTitle(self.windowTitle() + " *")  # optional visual indicator
    
    def closeEvent(self, event):
        if self.is_dirty:
            reply = QMessageBox.question(
                self,
                "Unsaved Changes",
                "You have unsaved changes. Do you want to save before exiting?",
                QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel
            )

            if reply == QMessageBox.Cancel:
                event.ignore()  # Cancel closing
                return
            elif reply == QMessageBox.Yes:
                self.save_data()
                if self.is_dirty:  # user canceled Save As dialog
                    event.ignore()
                    return

        event.accept()  # No unsaved changes, proceed with closing



    # ---------- Save / Load ----------
    def save_data(self):
        # If we already have a path, just save there
        if self.current_file_path:
            self._save_to_file(self.current_file_path)
        else:
            self.save_data_as()

    def save_data_as(self):
        path, _ = QFileDialog.getSaveFileName(self, "Save As", "", "JSON Files (*.json)")
        if not path:
            return
        self.current_file_path = path
        self._save_to_file(path)

    def _save_to_file(self, path):
        # Collect all data
        data = {
            "settings": self.settings,
            "nodes": [n.get_data() for n in self.node_blocks],
            "channels": [c.get_data() for c in self.channel_blocks],
        }
        with open(path, "w") as f:
            json.dump(data, f, indent=2)

        self.is_dirty = False
        self.setWindowTitle(app_name)


    def load_data(self):
        path,_=QFileDialog.getOpenFileName(self,"Load","","JSON Files (*.json)"); 
        if not path: return

        self.loading=True

        self.current_file_path = path

        with open(path) as f: data=json.load(f)

        # Load settings with defaults for any missing keys
        defaults = {
            "network_model": "32 nodes, 32 local channels",
            "priority_model": "Local Hierarchical",
            "priority_model_depth": 0,
            "sync_period_us": 1000000,
            "messages_path": "All",
            "target_language": "C/C++"
        }
        loaded_settings = data.get("settings", {})
        # Merge with defaults, preserving order
        self.settings = {k: loaded_settings.get(k, v) for k, v in defaults.items()}
        priority_model = self.settings.get('priority_model','Local Hierarchical')
        if priority_model == "Hybrid Hierarchical":
            depth = self.settings.get('priority_model_depth', 0)
            model_text = f"Hybrid {depth}-Hierarchical"
        else:
            model_text = priority_model
        self.graph_label.setText(f"Network Configuration — {model_text} Model")

        # Load descriptors
        self.reload_descriptors()

        # Clear existing
        for b in self.node_blocks+self.channel_blocks: b.setParent(None)
        self.node_blocks.clear(); self.channel_blocks.clear()

        # Load nodes
        for n in data.get("nodes",[]):
            self.add_node(); b=self.node_blocks[-1]; 
            b.name_edit.setText(n["name"])
            if n["node_id"] not in NODE_ID_OPTIONS: b.node_id_cb.setPlaceholderText(NODE_ID_PLACEHOLDER)
            b.node_id_cb.setCurrentText(n["node_id"])
            b.listener_cb.setChecked(bool(n.get("is_listener", False)))
            self.update_node_ids() # Must be updated to propagate ID changes due to misconfigurations in node n-1
            
        # Must be called after all nodes are added to propagate ID changes due to misconfigurations in node n
        self.update_node_ids()
        
        # Load channels
        for c in data.get("channels",[]):
            self.add_channel(); ch=self.channel_blocks[-1]
            ch.name_edit.setText(c["name"])
            for cb,val in [(ch.controller_cb,c["controller"]),(ch.responder_cb,c["responder"])]:
                if val and val not in [cb.itemText(i) for i in range(cb.count())]: cb.addItem(val)
                cb.setCurrentText(val)

        # Update message lists
        self.update_message_lists()

        for ch, c in zip(self.channel_blocks, data.get("channels", [])):
            for lane in [0,1]:
                combo = getattr(ch, f"lane{lane}_msg_cb")
                
                msg_descriptor = c.get(f"lane{lane}_message")

                # Set combo box to the message name if it exists
                if msg_descriptor:
                    message_name = msg_descriptor["message_name"]
                    index = combo.findText(message_name)
                    combo.setCurrentIndex(index if index != -1 else -1)
                else:
                    combo.setCurrentIndex(-1)

            # Set timeout
            timeout_edit = getattr(ch, f"lane0_to_edit")
            timeout_edit.setText(c.get(f"lane0_rx_timeout", ""))

                
        self.loading=False
        self.recompute_channels()
        self.is_dirty = False
        self.setWindowTitle(app_name)

        # --- Refocus main window to avoid focus issues ---
        QTimer.singleShot(1, lambda: self.setFocus(Qt.OtherFocusReason))  # Set focus to main window
        
        # Re-center graph after viewport has processed layout
        QTimer.singleShot(10, self.update_graph)

    def generate_network_configuration(self):
        """Validate configuration and compile network."""
        
        # Check all nodes are complete
        incomplete_nodes = []
        for idx, b in enumerate(self.node_blocks, start=1):
            name = b.name_edit.text()
            node_id = b.node_id_cb.currentText()
            if not name or node_id not in NODE_ID_OPTIONS:
                incomplete_nodes.append(f"Node {idx}: missing name or valid ID")
        
        if incomplete_nodes:
            QMessageBox.warning(self, "Validation Error", 
                              f"Incomplete nodes:\n" + "\n".join(incomplete_nodes))
            return
        
        # Check all channels are complete (all 7 fields filled)
        incomplete_channels = []
        for idx, b in enumerate(self.channel_blocks, start=1):
            has_all_fields = (b.name_edit.text() and 
                            b.controller_cb.currentText() and 
                            b.responder_cb.currentText() and
                            b.lane0_msg_cb.currentText() and
                            b.lane0_to_edit.text() and
                            b.lane1_msg_cb.currentText())
            if not has_all_fields:
                incomplete_channels.append(f"Channel {idx}: missing required fields")
        
        if incomplete_channels:
            QMessageBox.warning(self, "Validation Error", 
                              f"Incomplete channels:\n" + "\n".join(incomplete_channels))
            return
        
        # Check for duplicate names across nodes and channels
        all_names = []
        for b in self.node_blocks:
            name = b.name_edit.text().strip()
            if name:
                all_names.append(name)
        for b in self.channel_blocks:
            name = b.name_edit.text().strip()
            if name:
                all_names.append(name)
        
        # Find duplicates
        seen = set()
        duplicates = []
        for name in all_names:
            if name in seen:
                if name not in duplicates:
                    duplicates.append(name)
            seen.add(name)
        
        if duplicates:
            QMessageBox.warning(self, "Validation Error", 
                              f"Duplicate names found:\n" + "\n".join(duplicates))
            return
        
        # All validations passed - save and compile
        try:
            # Save to a temporary file if not already saved
            if not self.current_file_path:
                temp_dir = os.path.expanduser("~/.halo_configurator")
                os.makedirs(temp_dir, exist_ok=True)
                temp_path = os.path.join(temp_dir, "_temp_config.json")
            else:
                temp_path = self.current_file_path
            
            self._save_to_file(temp_path)
            
            # Import and call the compile function
            configurator_dir = Path(__file__).parent
            compilers_dir = configurator_dir / "Compilers"
            sys.path.insert(0, str(compilers_dir))
            
            try:
                from HALO_C_Compiler import compile_network_configuration
                compile_network_configuration(temp_path)
                QMessageBox.information(self, "Success", "Network configuration compiled successfully!")
            except ImportError as e:
                QMessageBox.critical(self, "Import Error", 
                                   f"Could not import compiler module:\n{str(e)}")
            except Exception as e:
                QMessageBox.critical(self, "Compilation Error", 
                                   f"Compilation failed:\n{str(e)}")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to generate configuration:\n{str(e)}")


if __name__=="__main__":
    app = QApplication(sys.argv)
    app_font = QFont("Consolas")
    app_font.setPointSize(10)
    app.setFont(app_font)
    win = App()
    win.show()
    sys.exit(app.exec())
