"""
axisflow_demo -- reference consumer of AxisFlow (NOT part of the AxisFlow core).

A minimal "draw a polyline" command that exercises the whole input cycle:
compass -> unit-aware entry -> axis lock -> confirmed point -> real geometry with
undo, a live preview of the in-progress polyline, and multi-line drawing.

Keys while drawing (defaults):
    click / Enter   place a point (Enter commits typed coordinates)
    Escape          finish THIS line (it is saved) and start a new one; the
                    compass and coordinate window stay up
    backtick  (`)   finish the current line AND end the command (tears down)

The working plane is picked up from FreeCAD's active working plane at start, so
points land on the plane the user is actually drawing on (not always world XY).

The preview is the consumer's job, not AxisFlow's: AxisFlow hands back points
and never guesses what is being drawn. Here it is a small, self-owned Coin
(pivy) node inserted into the active 3-D view's scene graph. The final geometry
is Draft.make_wire; each finished line is its own undo step.

Everything goes through the public getPoint() API; no AxisFlow internals used.

    import axisflow_demo
    axisflow_demo.line()          # draw; Escape starts a new line; ` ends
"""

import FreeCAD as App
import FreeCADGui as Gui

try:
    import AxisFlow as AF
except Exception as exc:      # pragma: no cover - only meaningful inside FreeCAD
    AF = None
    App.Console.PrintError("axisflow_demo: AxisFlow module not available: "
                           "{0}\n".format(exc))


def _sync_working_plane():
    """Push FreeCAD's active working plane into AxisFlow so the compass and the
    constrained points align with the plane the user is drawing on."""
    if AF is None:
        return
    wp = None
    try:
        import WorkingPlane
        try:
            wp = WorkingPlane.get_working_plane(update=False)
        except TypeError:
            wp = WorkingPlane.get_working_plane()
    except Exception:
        wp = getattr(App, "DraftWorkingPlane", None)
    if wp is None:
        return
    try:
        u = App.Vector(wp.u)
        v = App.Vector(wp.v)
        n = App.Vector(getattr(wp, "axis", None) or u.cross(v))
        AF.setPlane(u, v, n)
    except Exception as exc:
        App.Console.PrintWarning(
            "axisflow_demo: working-plane sync failed: {0}\n".format(exc))


class _LineDrawer(object):
    """Collects points from AxisFlow into Draft wires, with a live Coin preview
    managed entirely on the consumer side. Escape finishes a line and starts a
    new one; backtick ends the command."""

    def __init__(self):
        self._pts = []           # confirmed vertices of the CURRENT line
        # Segment i runs from _pts[i-1] to _pts[i]. If i is in _bulges it is an
        # ARC through that point instead of a straight segment -- a three-point
        # arc, which needs no tangency and works in any plane, so junctions may
        # meet at any angle and nothing is restricted to 2D.
        self._bulges = {}
        self._arc_mode = False    # the ToggleSegmentMode key flips this
        self._arc_end = None      # arc endpoint placed, waiting for the bulge
        self._snap_obj = None     # throw-away document object: see below
        self._sep = None          # our preview SoSeparator (in the view's SG)
        self._sg = None           # the scenegraph it was added to -- see below
        self._coords = None
        self._lineset = None
        self._doc_name = None     # resolved lazily, never cached as an object
        self._ended = False       # _end() is reachable from several directions

    # -- the document ------------------------------------------------------
    # A Document OBJECT must not be cached across the command: closing the file
    # leaves the wrapper pointing at something that no longer exists, and every
    # later openTransaction/addObject either raises or takes FreeCAD down. We
    # remember the NAME and look the document up each time, so a closed file is
    # simply "not found" and a different file is visibly a different document.
    def _doc(self):
        if self._doc_name is None:
            return None
        return App.getDocument(self._doc_name) if \
            self._doc_name in [d.Name for d in App.listDocuments().values()] else None

    def _doc_is_gone(self):
        """The document we started in has been closed, or another one took over."""
        doc = self._doc()
        if doc is None:
            return True
        active = App.ActiveDocument
        return active is not None and active.Name != self._doc_name

    # -- lifecycle ---------------------------------------------------------
    def start(self):
        doc = App.ActiveDocument or App.newDocument()
        self._doc_name = doc.Name
        _sync_working_plane()
        self._make_preview()
        doc.openTransaction("AxisFlow line")
        AF.getPoint(callback=self._on_point,
                    movecallback=self._on_move,
                    finishcallback=self._on_finish,   # Escape: finish + continue
                    modecallback=self._on_mode_toggle,
                    continuous=True)

    def _scene_graph(self):
        """None once the document (and with it the view) has gone away."""
        gdoc = getattr(Gui, "ActiveDocument", None)
        view = getattr(gdoc, "ActiveView", None) if gdoc else None
        return view.getSceneGraph() if view else None

    def _make_preview(self):
        try:
            from pivy import coin
            self._sep = coin.SoSeparator()
            color = coin.SoBaseColor()
            color.rgb = (1.0, 0.6, 0.1)
            style = coin.SoDrawStyle()
            style.lineWidth = 2.0
            self._coords = coin.SoCoordinate3()
            self._lineset = coin.SoLineSet()
            self._lineset.numVertices.setValue(0)
            self._sep.addChild(color)
            self._sep.addChild(style)
            self._sep.addChild(self._coords)
            self._sep.addChild(self._lineset)
            sg = self._scene_graph()
            if sg is None:
                raise RuntimeError("no active 3D view")
            sg.addChild(self._sep)
            # Remember WHICH scenegraph, and hold a reference to it. Asking for
            # the active view's scenegraph again at teardown is wrong the moment
            # the user switches documents: the separator lives in the old view's
            # graph, and removeChild() on a graph that does not contain it is an
            # out-of-range removal in Coin.
            self._sg = sg
            try:
                self._sg.ref()
            except Exception:
                pass
        except Exception as exc:
            self._sep = None
            App.Console.PrintWarning(
                "axisflow_demo: preview unavailable: {0}\n".format(exc))

    # -- geometry shared by the preview, the snap object and the commit -----
    def _edges(self, pts=None, bulges=None):
        """Part edges for the given vertices: a line, or an arc when the
        segment carries a bulge point. A degenerate arc (three collinear or
        coincident points has no circle) silently becomes a straight segment,
        which is what the user was drawing anyway."""
        import Part
        pts = self._pts if pts is None else pts
        bulges = self._bulges if bulges is None else bulges
        edges = []
        for i in range(1, len(pts)):
            a, b = pts[i - 1], pts[i]
            if (b - a).Length < 1e-9:
                continue
            through = bulges.get(i)
            if through is not None:
                try:
                    edges.append(Part.Arc(a, through, b).toShape())
                    continue
                except Exception:
                    pass
            edges.append(Part.LineSegment(a, b).toShape())
        return edges

    def _polyline_points(self, pts=None, bulges=None):
        """Vertices for the Coin preview, with arcs tessellated into segments."""
        try:
            import Part
            out = []
            for edge in self._edges(pts, bulges):
                # A straight edge needs its two ends; an arc needs sampling.
                count = 2 if isinstance(edge.Curve, Part.Line) else 24
                piece = edge.discretize(Number=count)
                if out and piece and (out[-1] - piece[0]).Length < 1e-9:
                    piece = piece[1:]        # shared vertex, do not repeat it
                out.extend(piece)
            return out
        except Exception:
            # Part unavailable, or an edge misbehaved: fall back to the raw
            # vertices so the preview degrades instead of disappearing.
            return list(self._pts if pts is None else pts)

    # -- the unfinished line as a snap target ------------------------------
    # Draft's Snapper only ever considers DOCUMENT OBJECTS: it picks them with
    # View.getObjectsInfo() and resolves the hit through
    # ActiveDocument.getObject(). A Coin node in the scenegraph, however
    # visible, simply does not exist as far as snapping is concerned -- which
    # is why the preview above could never be snapped to.
    #
    # Draft's own line tool solves this by creating a real Part::Feature the
    # moment the command starts and rewriting its Shape as points come in. We
    # do the same: the CONFIRMED part of the line lives in a throw-away
    # Part::Feature, so endpoint, midpoint, perpendicular and intersection
    # snapping onto our own unfinished line all work, for free, through the
    # normal Draft snapper. The Coin preview is then left with just the rubber
    # band to the cursor, so nothing is drawn twice.
    #
    # The object is removed before every commit, so it never reaches the saved
    # document or the undo stack.
    def _update_snap_object(self):
        if not self._pts:
            self._remove_snap_object()
            return
        try:
            import Part
            # One point is still worth publishing: its vertex can be snapped to.
            if len(self._pts) == 1:
                shape = Part.Vertex(self._pts[0])
            else:
                edges = self._edges()
                if not edges:
                    self._remove_snap_object()
                    return
                # A wire of mixed line and arc edges, so Draft's snapper offers
                # arc centres and midpoints on our own unfinished geometry too.
                shape = Part.Wire(edges)
            if self._snap_obj is None:
                doc = self._doc()
                if doc is None:
                    return
                self._snap_obj = doc.addObject("Part::Feature",
                                               "AxisFlow_Drawing")
                self._style_snap_object()
            self._snap_obj.Shape = shape
        except Exception as exc:
            # Never let this break drawing: without it we simply lose
            # self-snapping and fall back to the all-Coin preview.
            App.Console.PrintWarning(
                "axisflow_demo: self-snapping unavailable: {0}\n".format(exc))
            self._remove_snap_object()

    def _style_snap_object(self):
        """Make it look exactly like the old preview, and stay snappable."""
        vo = getattr(self._snap_obj, "ViewObject", None)
        if vo is None:
            return
        try:
            vo.LineColor = (1.0, 0.6, 0.1)
            vo.PointColor = (1.0, 0.6, 0.1)
            vo.LineWidth = 2.0
            vo.PointSize = 4.0
            # The Snapper skips objects whose ViewObject is not selectable.
            vo.Selectable = True
        except Exception:
            pass

    def _remove_snap_object(self):
        if self._snap_obj is None:
            return
        try:
            doc = self._doc()
            if doc is not None:
                doc.removeObject(self._snap_obj.Name)
        except Exception:
            pass
        self._snap_obj = None

    def _update_preview(self, cursor=None):
        if self._sep is None:
            return
        if self._arc_end is not None and cursor is not None:
            # An arc endpoint is placed and the cursor is choosing the bulge:
            # show the arc that would be committed right now.
            pts = self._polyline_points(
                [self._pts[-1], self._arc_end],
                {1: App.Vector(cursor)})
        elif self._snap_obj is not None:
            # The confirmed part is on screen as the temporary object, so all
            # that is left here is the rubber band from the last point.
            pts = ([self._pts[-1], App.Vector(cursor)]
                   if (cursor is not None and self._pts) else [])
        else:
            # No temporary object (Part missing, or it failed): draw everything
            # here, exactly as before.
            pts = list(self._pts)
            if cursor is not None:
                pts.append(App.Vector(cursor))
        if len(pts) >= 2:
            self._coords.point.setValues(0, len(pts), [tuple(p) for p in pts])
            self._coords.point.setNum(len(pts))
            self._lineset.numVertices.setValue(len(pts))
        else:
            self._lineset.numVertices.setValue(0)

    def _clear_selection(self):
        # A freshly made Draft wire is auto-selected (highlighted). Clear it so
        # the finished line looks like plain, unselected geometry.
        try:
            Gui.Selection.clearSelection()
        except Exception:
            pass

    def _remove_preview(self):
        if self._sep is None:
            return
        try:
            if self._sg is not None:
                self._sg.removeChild(self._sep)   # the graph we added it to
        except Exception:
            pass
        try:
            if self._sg is not None:
                self._sg.unref()
        except Exception:
            pass
        self._sg = None
        self._sep = None
        self._coords = None
        self._lineset = None

    # -- point stream ------------------------------------------------------
    def _abandon_if_document_changed(self):
        """Closing the file, or switching to another one, ends the command.

        Carrying on would either write into a document that no longer exists
        (transactions on a dead wrapper) or, worse, quietly keep adding
        geometry to a file the user is no longer looking at."""
        if not self._doc_is_gone():
            return False
        App.Console.PrintMessage(
            "axisflow_demo: the document changed, ending the command\n")
        # The scratch object belongs to the document we STARTED in, and _doc()
        # resolves by that name, so this removes it from the right place rather
        # than leaving it orphaned there. (The transaction abort below would
        # roll it back too, but relying on that is a trap for the day the
        # transaction handling changes.)
        self._remove_snap_object()
        # The half-drawn line is discarded rather than committed: the user is
        # looking at another document now, and silently dropping geometry into
        # the one they left is worse than losing an unfinished line.
        self._pts = []
        self._bulges = {}
        self._arc_end = None
        self._end()
        return True

    def _on_point(self, point, ok):
        if not ok:                       # backtick / cancel -> end the command
            self._end()
            return
        if self._abandon_if_document_changed():
            return

        p = App.Vector(point)

        # An arc takes THREE points and the first is the segment we are
        # continuing from, so two clicks are left: the endpoint, then a point
        # anywhere along the arc. No tangency is imposed, which is why segments
        # may meet at any angle, and three points define their own plane, which
        # is why nothing here is restricted to 2D.
        if self._arc_mode and self._pts:
            if self._arc_end is None:
                start = self._pts[-1]
                self._arc_end = p
                # The bulge is measured from the CHORD, so the compass belongs
                # at its midpoint while that point is being picked: distance
                # from there is the arc's height, and the axes straddle the
                # chord. Confirming the endpoint has just anchored the compass
                # on it, so this moves it back.
                self._move_compass((start + p) * 0.5)
                App.Console.PrintMessage(
                    "axisflow_demo: arc endpoint set; pick a point on the arc\n")
                self._update_preview()
                return
            through, end = p, self._arc_end
            self._arc_end = None
            self._pts.append(end)
            self._bulges[len(self._pts) - 1] = through
            # Confirming the bulge point anchored the compass THERE, which is
            # not where the polyline continues from: the next segment starts at
            # the arc's end.
            self._move_compass(end)
            App.Console.PrintMessage(
                "axisflow_demo: arc {0} -> {1}\n".format(len(self._pts), end))
        else:
            self._pts.append(p)
            App.Console.PrintMessage(
                "axisflow_demo: point {0} -> {1}\n".format(len(self._pts), p))

        self._update_snap_object()   # the new segment becomes snappable
        self._update_preview()

    def _on_move(self, point):
        if self._abandon_if_document_changed():
            return
        self._update_preview(cursor=point)

    def _move_compass(self, point):
        """Park the compass anchor at @p point without confirming anything."""
        try:
            AF.setBasePoint(App.Vector(point))
        except Exception as exc:
            App.Console.PrintWarning(
                "axisflow_demo: cannot move the compass: {0}\n".format(exc))

    def _on_mode_toggle(self):
        """The ToggleSegmentMode key: straight segments <-> arcs.

        AxisFlow only relays the keypress; what the mode means is decided here.
        The mode persists until toggled back, like a polyline tool's arc mode.
        Toggling mid-arc abandons the half-placed arc rather than leaving an
        endpoint hanging."""
        self._arc_mode = not self._arc_mode
        if self._arc_end is not None:
            self._arc_end = None
            # The compass was parked on the chord midpoint: bring it back to
            # where the polyline actually continues from.
            if self._pts:
                self._move_compass(self._pts[-1])
            App.Console.PrintMessage("axisflow_demo: arc abandoned\n")
        App.Console.PrintMessage(
            "axisflow_demo: segments are now {0}\n".format(
                "arcs (endpoint, then a point on the arc)" if self._arc_mode
                else "straight"))
        self._update_preview()

    def _on_finish(self):
        # Escape: save the current line, keep the session, start a new line.
        # The scratch object goes first: it must never reach the commit.
        self._remove_snap_object()
        doc = self._doc()
        if doc is None:
            # The file was closed while the line was being drawn: there is
            # nothing left to commit to, so end instead of pretending.
            self._end()
            return
        drew = self._commit_current_wire()
        if drew:
            doc.commitTransaction()
            doc.recompute()
            self._clear_selection()
        else:
            doc.abortTransaction()
        self._pts = []
        self._bulges = {}
        self._arc_end = None      # a half-placed arc is not committed
        self._update_preview()                    # clears the rubber band
        doc.openTransaction("AxisFlow line")      # ready for the next line

    # -- geometry ----------------------------------------------------------
    def _commit_current_wire(self):
        """Turn the current segments into a document object. True if drawn."""
        if len(self._pts) < 2:
            return False
        try:
            if not self._bulges:
                # All straight: keep making a Draft wire, which stays editable
                # by its points -- no reason to lose that when there are no arcs.
                import Draft
                Draft.make_wire(self._pts, closed=False)
                App.Console.PrintMessage(
                    "axisflow_demo: wire with {0} points\n".format(len(self._pts)))
                return True

            # Mixed line/arc: Draft's wire cannot hold arcs, so the result is a
            # plain Part feature carrying one wire of line and arc edges. Not
            # parametric -- deliberately, since an editable mixed polyline would
            # need a scripted object of its own.
            import Part
            edges = self._edges()
            if not edges:
                return False
            doc = self._doc()
            if doc is None:
                return False
            obj = doc.addObject("Part::Feature", "AxisFlow_Polyline")
            obj.Shape = Part.Wire(edges)
            App.Console.PrintMessage(
                "axisflow_demo: polyline with {0} segments ({1} arc(s))\n".format(
                    len(edges), len(self._bulges)))
            return True
        except Exception as exc:
            App.Console.PrintError("axisflow_demo: {0}\n".format(exc))
            return False

    def _end(self):
        # Final teardown: commit whatever is in progress, then end the session.
        # Reachable from the cancel key, from the document-changed check, and
        # from line() starting a new run -- and each of those can happen while
        # another is in flight, so it has to be a no-op the second time.
        if self._ended:
            return
        self._ended = True
        self._remove_snap_object()
        doc = self._doc()
        drew = self._commit_current_wire() if doc is not None else False
        if doc is not None:
            if drew:
                doc.commitTransaction()
                doc.recompute()
                self._clear_selection()
            else:
                doc.abortTransaction()
        App.Console.PrintMessage("axisflow_demo: ended\n")
        self._remove_preview()
        try:
            AF.endSession()
        except Exception:
            pass


_active = None


def line():
    """Start the demo line command."""
    global _active
    if AF is None:
        App.Console.PrintError("axisflow_demo: AxisFlow not loaded\n")
        return
    # Tear the previous run down PROPERLY instead of letting the garbage
    # collector have it. Dropping the reference leaves its Coin separator in
    # the old view's scenegraph, its extra reference on that scenegraph, its
    # scratch object in the old document and its callbacks registered -- and
    # pivy then frees the Python wrappers of nodes that are still in a live
    # graph. That is exactly the state the crash was reported from: draw,
    # create a new file, start drawing again.
    if _active is not None:
        try:
            _active._end()
        except Exception as exc:
            App.Console.PrintWarning(
                "axisflow_demo: could not end the previous line: {0}\n".format(exc))
        _active = None

    _active = _LineDrawer()
    _active.start()
    return _active
