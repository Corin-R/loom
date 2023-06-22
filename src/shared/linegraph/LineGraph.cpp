// Copyright 2017, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Authors: Patrick Brosi <brosi@informatik.uni-freiburg.de>

#include "3rdparty/json.hpp"
#include "dot/Parser.h"
#include "shared/linegraph/LineEdgePL.h"
#include "shared/linegraph/LineGraph.h"
#include "shared/linegraph/LineNodePL.h"
#include "shared/style/LineStyle.h"
#include "util/String.h"
#include "util/graph/Edge.h"
#include "util/graph/Node.h"
#include "util/log/Log.h"

using shared::linegraph::EdgeGrid;
using shared::linegraph::EdgeOrdering;
using shared::linegraph::ISect;
using shared::linegraph::Line;
using shared::linegraph::LineEdge;
using shared::linegraph::LineGraph;
using shared::linegraph::LineNode;
using shared::linegraph::LineOcc;
using shared::linegraph::NodeGrid;
using shared::linegraph::Partner;
using util::geo::DPoint;
using util::geo::Point;

// _____________________________________________________________________________
void LineGraph::readFromDot(std::istream* s, double smooth) {
  UNUSED(smooth);
  _bbox = util::geo::Box<double>();

  dot::parser::Parser dp(s);
  std::map<std::string, LineNode*> idMap;

  size_t eid = 0;

  while (dp.has()) {
    auto ent = dp.get();

    if (ent.type == dot::parser::EMPTY) {
      continue;
    } else if (ent.type == dot::parser::NODE) {
      // only use nodes with a position
      if (ent.attrs.find("pos") == ent.attrs.end()) continue;
      std::string coords = ent.attrs["pos"];
      std::replace(coords.begin(), coords.end(), ',', ' ');

      std::stringstream ss;
      ss << coords;

      double x, y;

      ss >> x;
      ss >> y;

      LineNode* n = 0;
      if (idMap.find(ent.ids.front()) != idMap.end()) {
        n = idMap[ent.ids.front()];
      }

      if (!n) {
        n = addNd(util::geo::Point<double>(x, y));
        idMap[ent.ids[0]] = n;
      }

      expandBBox(*n->pl().getGeom());

      Station i("", "", *n->pl().getGeom());
      if (ent.attrs.find("station_id") != ent.attrs.end() ||
          ent.attrs.find("label") != ent.attrs.end()) {
        if (ent.attrs.find("station_id") != ent.attrs.end())
          i.id = ent.attrs["station_id"];
        if (ent.attrs.find("label") != ent.attrs.end())
          i.name = ent.attrs["label"];
        n->pl().addStop(i);
      }
    } else if (ent.type == dot::parser::EDGE) {
      eid++;
      std::string prevId = ent.ids.front();
      if (idMap.find(prevId) == idMap.end())
        idMap[prevId] = addNd(util::geo::Point<double>(0, 0));

      for (size_t i = 1; i < ent.ids.size(); ++i) {
        std::string curId = ent.ids[i];

        if (idMap.find(curId) == idMap.end())
          idMap[curId] = addNd(util::geo::Point<double>(0, 0));
        auto e = getEdg(idMap[prevId], idMap[curId]);

        if (!e) {
          PolyLine<double> pl;
          e = addEdg(idMap[curId], idMap[prevId], pl);
        }

        std::string id;
        if (ent.attrs.find("id") != ent.attrs.end()) {
          id = ent.attrs["id"];
        } else if (ent.attrs.find("label") != ent.attrs.end()) {
          id = ent.attrs["label"];
        } else if (ent.attrs.find("color") != ent.attrs.end()) {
          id = ent.attrs["color"];
        } else {
          id = util::toString(eid);
        }

        const Line* r = getLine(id);
        if (!r) {
          std::string label = ent.attrs.find("label") == ent.attrs.end()
                                  ? ""
                                  : ent.attrs["label"];
          std::string color = ent.attrs.find("color") == ent.attrs.end()
                                  ? ""
                                  : ent.attrs["color"];
          r = new Line(id, label, color);
          addLine(r);
        }

        LineNode* dir = 0;

        if (ent.graphType == dot::parser::DIGRAPH ||
            ent.graphType == dot::parser::STRICT_DIGRAPH) {
          dir = idMap[curId];
        }

        e->pl().addLine(r, dir);
      }
    }
  }

  for (auto n : getNds()) {
    for (auto e : n->getAdjListOut()) {
      PolyLine<double> pl;
      pl << *e->getFrom()->pl().getGeom();
      pl << *e->getTo()->pl().getGeom();

      e->pl().setPolyline(pl);
      expandBBox(pl.front());
      expandBBox(pl.back());
    }
  }

  _bbox = util::geo::pad(_bbox, 100);
  buildGrids();
}

// _____________________________________________________________________________
void LineGraph::readFromTopoJson(nlohmann::json::array_t objects,
                                 nlohmann::json::array_t arcs, double smooth) {
  UNUSED(objects);
  UNUSED(arcs);
  UNUSED(smooth);
  throw(std::runtime_error("TopoJSON input not yet implemented."));
}

// _____________________________________________________________________________
void LineGraph::readFromGeoJson(nlohmann::json::array_t features,
                                double smooth) {
  _bbox = util::geo::Box<double>();

  std::map<std::string, LineNode*> idMap;

  for (auto feature : features) {
    auto props = feature["properties"];
    auto geom = feature["geometry"];
    if (geom["type"] == "Point") {
      std::string id = props["id"].get<std::string>();

      std::vector<double> coords = geom["coordinates"];

      LineNode* n = addNd(util::geo::DPoint(coords[0], coords[1]));
      expandBBox(*n->pl().getGeom());

      Station i("", "", *n->pl().getGeom());
      if (!props["station_id"].is_null() || !props["station_label"].is_null()) {
        if (!props["station_id"].is_null()) {
          if (props["station_id"].type() == nlohmann::json::value_t::string) {
            i.id = props["station_id"].get<std::string>();
          } else {
            i.id = props["station_id"].get<std::string>();
          }
        }
        if (!props["station_label"].is_null()) i.name = props["station_label"];
        n->pl().addStop(i);
      }

      idMap[id] = n;
    }
  }

  // second pass, edges
  for (auto feature : features) {
    auto props = feature["properties"];
    auto geom = feature["geometry"];
    if (geom["type"] == "LineString") {
      if (props["lines"].is_null() || props["lines"].size() == 0) continue;
      std::string from = props["from"].get<std::string>();
      std::string to = props["to"].get<std::string>();

      std::vector<std::vector<double>> coords = geom["coordinates"];

      PolyLine<double> pl;
      for (auto coord : coords) {
        double x = coord[0], y = coord[1];
        Point<double> p(x, y);
        pl << p;
        expandBBox(p);
      }

      pl.applyChaikinSmooth(smooth);

      LineNode* fromN = 0;
      LineNode* toN = 0;

      if (from.size()) {
        fromN = idMap[from];
        if (!fromN) {
          LOG(ERROR) << "Node \"" << from << "\" not found." << std::endl;
          continue;
        }
      } else {
        fromN = addNd(pl.getLine().front());
      }

      if (to.size()) {
        toN = idMap[to];
        if (!fromN) {
          LOG(ERROR) << "Node \"" << to << "\" not found." << std::endl;
          continue;
        }
      } else {
        fromN = addNd(pl.getLine().back());
      }

      LineEdge* e = addEdg(fromN, toN, pl);

      if (props["dontcontract"].is_number() && props["dontcontract"].get<int>())
        e->pl().setDontContract(true);

      for (auto line : props["lines"]) {
        std::string id;
        if (!line["id"].is_null()) {
          id = line["id"].get<std::string>();
        } else if (!line["label"].is_null()) {
          id = line["label"].get<std::string>();
        } else if (!line["color"].is_null()) {
          id = line["color"].get<std::string>();
        } else
          continue;

        const Line* l = getLine(id);
        if (!l) {
          std::string label = line["label"].is_null() ? "" : line["label"];
          std::string color = line["color"];

          std::string myLabel;
          if(!line["startLabel"].is_null()){
            myLabel = line["startLabel"];
          }else{
            myLabel = "";
          }

          std::string backLabel;
          if(!line["backLabel"].is_null()){
            backLabel = line["backLabel"];
          }else{
            backLabel = "";
          }


          l = new Line(id, label, color, myLabel, backLabel, from, to);
          addLine(l);
        }

        LineNode* dir = 0;

        if (!line["direction"].is_null()) {
          dir = idMap[line["direction"].get<std::string>()];
        }

        if (!line["style"].is_null() || !line["outline-style"].is_null()) {
          shared::style::LineStyle ls;

          if (!line["style"].is_null()) ls.setCss(line["style"]);
          if (!line["outline-style"].is_null())
            ls.setOutlineCss(line["outline-style"]);

          e->pl().addLine(l, dir, ls);
        } else {
          e->pl().addLine(l, dir);
        }
      }
    }
  }

  // third pass, exceptions (TODO: do this in the first part, store in some
  // data structure, add here!)
  for (auto feature : features) {
    auto props = feature["properties"];
    auto geom = feature["geometry"];
    if (geom["type"] == "Point") {
      std::string id = props["id"].get<std::string>();

      if (!idMap.count(id)) continue;
      LineNode* n = idMap[id];

      if (!props["not_serving"].is_null()) {
        for (auto excl : props["not_serving"]) {
          std::string lid = excl.get<std::string>();

          const Line* r = getLine(lid);

          if (!r) {
            LOG(WARN) << "line " << lid << " marked as not served in in node "
                      << id << ", but no such line exists.";
            continue;
          }

          n->pl().addLineNotServed(r);
        }
      }

      if (!props["excluded_line_conns"].is_null()) {
        for (auto excl : props["excluded_line_conns"]) {
          std::string lid = excl["route"].get<std::string>();
          std::string nid1 = excl["edge1_node"].get<std::string>();
          std::string nid2 = excl["edge2_node"].get<std::string>();

          const Line* r = getLine(lid);

          if (!r) {
            LOG(WARN) << "line connection exclude defined in node " << id
                      << " for line " << lid << ", but no such line exists.";
            continue;
          }

          if (!idMap.count(nid1)) {
            LOG(WARN) << "line connection exclude defined in node " << id
                      << " for edge from " << nid1
                      << ", but no such node exists.";
            continue;
          }

          if (!idMap.count(nid2)) {
            LOG(WARN) << "line connection exclude defined in node " << id
                      << " for edge from " << nid2
                      << ", but no such node exists.";
            continue;
          }

          LineNode* n1 = idMap[nid1];
          LineNode* n2 = idMap[nid2];

          LineEdge* a = getEdg(n, n1);
          LineEdge* b = getEdg(n, n2);

          if (!a) {
            LOG(WARN) << "line connection exclude defined in node " << id
                      << " for edge from " << nid1
                      << ", but no such edge exists.";
            continue;
          }

          if (!b) {
            LOG(WARN) << "line connection exclude defined in node " << id
                      << " for edge from " << nid2
                      << ", but no such edge exists.";
            continue;
          }

          n->pl().addConnExc(r, a, b);
        }
      }
    }
  }

  _bbox = util::geo::pad(_bbox, 100);

  buildGrids();
}

// _____________________________________________________________________________
void LineGraph::readFromJson(std::istream* s, double smooth) {
  nlohmann::json j;
  (*s) >> j;

  if (j["type"] == "FeatureCollection") readFromGeoJson(j["features"], smooth);
  if (j["type"] == "Topology")
    readFromTopoJson(j["objects"], j["arcs"], smooth);
}

// _____________________________________________________________________________
void LineGraph::buildGrids() {
  size_t gridSize =
      std::max(_bbox.getUpperRight().getX() - _bbox.getLowerLeft().getX(),
               _bbox.getUpperRight().getY() - _bbox.getLowerLeft().getY()) /
      10;

  _nodeGrid = NodeGrid(gridSize, gridSize, _bbox);
  _edgeGrid = EdgeGrid(gridSize, gridSize, _bbox);

  for (auto n : getNds()) {
    _nodeGrid.add(*n->pl().getGeom(), n);
    for (auto e : n->getAdjListOut()) {
      _edgeGrid.add(*e->pl().getGeom(), e);
    }
  }
}

// _____________________________________________________________________________
void LineGraph::expandBBox(const Point<double>& p) {
  _bbox = util::geo::extendBox(p, _bbox);
}

// _____________________________________________________________________________
const util::geo::DBox& LineGraph::getBBox() const { return _bbox; }

// _____________________________________________________________________________
void LineGraph::topologizeIsects() {
  proced.clear();
  while (getNextIntersection().a) {
    auto i = getNextIntersection();
    auto x = addNd(i.bp.p);

    double pa = i.a->pl().getPolyline().projectOn(i.bp.p).totalPos;

    auto ba = addEdg(i.b->getFrom(), x, i.b->pl());
    ba->pl().setPolyline(i.b->pl().getPolyline().getSegment(0, i.bp.totalPos));

    auto bb = addEdg(x, i.b->getTo(), i.b->pl());
    bb->pl().setPolyline(i.b->pl().getPolyline().getSegment(i.bp.totalPos, 1));

    edgeRpl(i.b->getFrom(), i.b, ba);
    edgeRpl(i.b->getTo(), i.b, bb);

    nodeRpl(ba, i.b->getTo(), x);
    nodeRpl(bb, i.b->getFrom(), x);

    _edgeGrid.add(*ba->pl().getGeom(), ba);
    _edgeGrid.add(*bb->pl().getGeom(), bb);

    auto aa = addEdg(i.a->getFrom(), x, i.a->pl());
    aa->pl().setPolyline(i.a->pl().getPolyline().getSegment(0, pa));
    auto ab = addEdg(x, i.a->getTo(), i.a->pl());
    ab->pl().setPolyline(i.a->pl().getPolyline().getSegment(pa, 1));

    edgeRpl(i.a->getFrom(), i.a, aa);
    edgeRpl(i.a->getTo(), i.a, ab);

    nodeRpl(aa, i.b->getTo(), x);
    nodeRpl(ab, i.b->getFrom(), x);

    _edgeGrid.add(*aa->pl().getGeom(), aa);
    _edgeGrid.add(*ab->pl().getGeom(), ab);

    _edgeGrid.remove(i.a);
    _edgeGrid.remove(i.b);

    assert(getEdg(i.a->getFrom(), i.a->getTo()));
    assert(getEdg(i.b->getFrom(), i.b->getTo()));
    delEdg(i.a->getFrom(), i.a->getTo());
    delEdg(i.b->getFrom(), i.b->getTo());
  }
}

// _____________________________________________________________________________
std::set<LineEdge*> LineGraph::getNeighborEdges(const util::geo::DLine& line,
                                                double d) const {
  std::set<LineEdge*> neighbors;
  _edgeGrid.get(line, d, &neighbors);

  return neighbors;
}

// _____________________________________________________________________________
ISect LineGraph::getNextIntersection() {
  for (auto n1 : getNds()) {
    for (auto e1 : n1->getAdjList()) {
      if (e1->getFrom() != n1) continue;
      if (proced.find(e1) != proced.end()) continue;

      std::set<LineEdge*> neighbors;
      _edgeGrid.getNeighbors(e1, 0, &neighbors);

      for (auto e2 : neighbors) {
        if (proced.find(e2) != proced.end()) continue;
        if (e1 != e2) {
          auto is =
              e1->pl().getPolyline().getIntersections(e2->pl().getPolyline());

          if (is.size()) {
            ISect ret;
            ret.a = e1;
            ret.b = e2;
            ret.bp = *is.begin();
            // if the intersection is near a shared node, ignore
            auto shrdNd = sharedNode(e1, e2);
            if (shrdNd &&
                util::geo::dist(*shrdNd->pl().getGeom(), ret.bp.p) < 100) {
              continue;
            }

            if (ret.bp.totalPos > 0.001 && 1 - ret.bp.totalPos > 0.001) {
              return ret;
            }
          }
        }
      }
      proced.insert(e1);
    }
  }

  ISect ret;
  ret.a = 0;
  ret.b = 0;
  return ret;
}

// _____________________________________________________________________________
void LineGraph::addLine(const Line* l) { _lines[l->id()] = l; }

// _____________________________________________________________________________
const Line* LineGraph::getLine(const std::string& id) const {
  if (_lines.find(id) != _lines.end()) return _lines.find(id)->second;
  return 0;
}

// _____________________________________________________________________________
bool LineGraph::lineCtd(const LineEdge* frEdg, const LineEdge* toEdg,
                        const Line* line) {
  if (!frEdg->pl().hasLine(line) || !toEdg->pl().hasLine(line)) return false;
  auto frLn = frEdg->pl().lineOcc(line);
  auto toLn = toEdg->pl().lineOcc(line);
  return lineCtd(frEdg, frLn, toEdg, toLn);
}

// _____________________________________________________________________________
bool LineGraph::lineCtd(const LineEdge* frEdg, const LineOcc& frLn,
                        const LineEdge* toEdg, const LineOcc& toLn) {
  if (frLn.line != toLn.line) return false;
  const auto* n = sharedNode(frEdg, toEdg);
  if (!n || n->getDeg() == 1) return false;
  return (frLn.direction == 0 || toLn.direction == 0 ||
          (frLn.direction == n && toLn.direction != n) ||
          (frLn.direction != n && toLn.direction == n)) &&
         n->pl().connOccurs(frLn.line, frEdg, toEdg);
}

// _____________________________________________________________________________
std::vector<LineOcc> LineGraph::getCtdLinesIn(const LineOcc& frLn,
                                              const LineEdge* frEdge,
                                              const LineEdge* toEdge) {
  std::vector<LineOcc> ret;
  const auto* n = sharedNode(frEdge, toEdge);
  if (!n || n->getDeg() == 1) return ret;

  for (const auto& toLn : toEdge->pl().getLines()) {
    if (lineCtd(frEdge, frLn, toEdge, toLn)) ret.push_back(toLn);
  }

  return ret;
}

// _____________________________________________________________________________
std::vector<LineOcc> LineGraph::getCtdLinesIn(const LineEdge* fromEdge,
                                              const LineEdge* toEdge) {
  std::vector<LineOcc> ret;
  const auto* n = sharedNode(fromEdge, toEdge);
  if (!n) return ret;

  for (const auto& to : fromEdge->pl().getLines()) {
    auto r = getCtdLinesIn(to, fromEdge, toEdge);
    ret.insert(ret.end(), r.begin(), r.end());
  }

  return ret;
}

// _____________________________________________________________________________
size_t LineGraph::getLDeg(const LineNode* nd) {
  size_t ret = 0;
  for (auto e : nd->getAdjList()) ret += e->pl().getLines().size();
  return ret;
}

// _____________________________________________________________________________
size_t LineGraph::getMaxLineNum(const LineNode* nd) {
  size_t ret = 0;
  for (auto e : nd->getAdjList())
    if (e->pl().getLines().size() > ret) ret = e->pl().getLines().size();
  return ret;
}

// _____________________________________________________________________________
size_t LineGraph::getMaxLineNum() const {
  size_t ret = 0;
  for (auto nd : getNds()) {
    size_t lineNum = getMaxLineNum(nd);
    if (lineNum > ret) ret = lineNum;
  }
  return ret;
}

// _____________________________________________________________________________
size_t LineGraph::maxDeg() const {
  size_t ret = 0;
  for (auto nd : getNds())
    if (nd->getDeg() > ret) ret = nd->getDeg();
  return ret;
}

// _____________________________________________________________________________
std::vector<const Line*> LineGraph::getSharedLines(const LineEdge* a,
                                                   const LineEdge* b) {
  std::vector<const Line*> ret;
  for (auto& to : a->pl().getLines()) {
    if (b->pl().hasLine(to.line)) ret.push_back(to.line);
  }

  return ret;
}

// _____________________________________________________________________________
size_t LineGraph::numLines() const { return _lines.size(); }

// _____________________________________________________________________________
size_t LineGraph::numNds() const { return getNds().size(); }

// _____________________________________________________________________________
size_t LineGraph::numNds(bool topo) const {
  size_t ret = 0;
  for (auto nd : getNds()) {
    if ((nd->pl().stops().size() == 0) ^ !topo) ret++;
  }
  return ret;
}

// _____________________________________________________________________________
size_t LineGraph::numEdgs() const {
  size_t ret = 0;
  for (auto nd : getNds()) {
    for (auto e : nd->getAdjList()) {
      if (e->getFrom() != nd) continue;
      ret++;
    }
  }
  return ret;
}

// _____________________________________________________________________________
NodeGrid* LineGraph::getNdGrid() { return &_nodeGrid; }

// _____________________________________________________________________________
const NodeGrid& LineGraph::getNdGrid() const { return _nodeGrid; }

// _____________________________________________________________________________
EdgeGrid* LineGraph::getEdgGrid() { return &_edgeGrid; }

// _____________________________________________________________________________
const EdgeGrid& LineGraph::getEdgGrid() const { return _edgeGrid; }

// _____________________________________________________________________________
std::set<const shared::linegraph::Line*> LineGraph::servedLines(
    const shared::linegraph::LineNode* n) {
  std::set<const shared::linegraph::Line*> ret;

  for (auto e : n->getAdjList()) {
    for (auto l : e->pl().getLines()) {
      if (n->pl().lineServed(l.line)) {
        ret.insert(l.line);
      }
    }
  }
  return ret;
}

// _____________________________________________________________________________
EdgeOrdering LineGraph::edgeOrdering(LineNode* n, bool useOrigNextNode) {
  EdgeOrdering order;
  util::geo::DPoint a = *n->pl().getGeom();

  for (auto e : n->getAdjList()) {
    util::geo::DPoint b;
    if (useOrigNextNode) {
      b = *e->getOtherNd(n)->pl().getGeom();
    } else {
      auto other = e->getOtherNd(n);
      if (e->pl().getGeom()->size() > 2) {
        if (e->getTo() == n) {
          b = e->pl().getGeom()->at(e->pl().getGeom()->size() - 2);
        } else {
          b = e->pl().getGeom()->at(1);
        }
      } else {
        b = *other->pl().getGeom();
      }
    }

    // get the angles
    double deg = util::geo::angBetween(a, b) - M_PI / 2;
    // make sure the ordering start at 12 o'clock
    if (deg <= 0) deg += M_PI * 2;

    order.add(e, deg);
  }

  return order;
}

// _____________________________________________________________________________
void LineGraph::splitNode(LineNode* n, size_t maxDeg) {
  assert(maxDeg > 2);

  if (n->getAdjList().size() > maxDeg) {
    std::vector<std::pair<LineEdge*, double>> combine;

    const auto& eo = edgeOrdering(n, true);
    const auto& orig = eo.getOrderedSet();

    combine.insert(combine.begin(), orig.begin() + (maxDeg - 1), orig.end());

    // for the new geometry, take the average angle
    double refAngle = 0;
    util::geo::MultiPoint<double> mp;

    for (auto eo : combine) {
      mp.push_back(*eo.first->getOtherNd(n)->pl().getGeom());
    }
    refAngle = util::geo::angBetween(*n->pl().getGeom(), mp);

    auto geom = *n->pl().getGeom();
    geom.setX(geom.getX() + 10 * cos(refAngle));
    geom.setY(geom.getY() + 10 * sin(refAngle));
    // add a new node
    auto cn = addNd(geom);

    // add the new trunk edge
    auto ce =
        addEdg(n, cn, LineEdgePL({*n->pl().getGeom(), *cn->pl().getGeom()}));

    for (auto eo : combine) {
      LineEdge* newEdg = 0;
      if (eo.first->getFrom() == n) {
        newEdg = addEdg(cn, eo.first->getOtherNd(n), eo.first->pl());
      } else {
        newEdg = addEdg(eo.first->getOtherNd(n), cn, eo.first->pl());
      }

      // replace direction markers in the new edge
      nodeRpl(newEdg, n, cn);

      // replace exception containing the old edge in the remaining target node
      edgeRpl(eo.first->getOtherNd(n), eo.first, newEdg);

      for (auto lo : eo.first->pl().getLines()) {
        ce->pl().addLine(lo.line, 0);
      }

      // in the old node, replace any exception occurence of this node with
      // the new trunk edge
      edgeRpl(n, eo.first, ce);
      _edgeGrid.remove(eo.first);
      delEdg(eo.first->getFrom(), eo.first->getTo());
    }

    // no continuation lines across the new node, only to the trunk edge
    for (auto lo : ce->pl().getLines()) {
      for (auto ea : cn->getAdjList()) {
        if (ea == ce) continue;
        for (auto eb : cn->getAdjList()) {
          if (eb == ce) continue;
          if (ea == eb) continue;
          cn->pl().addConnExc(lo.line, ea, eb);
        }
      }
    }

    assert(n->getDeg() <= maxDeg);

    // recursively split until max deg is satisfied
    if (cn->getDeg() > maxDeg) splitNode(cn, maxDeg);
  }
}

// _____________________________________________________________________________
void LineGraph::splitNodes(size_t maxDeg) {
  std::vector<LineNode*> toSplit;
  for (auto n : getNds()) {
    if (n->getDeg() > maxDeg) toSplit.push_back(n);
  }
  for (auto n : toSplit) splitNode(n, maxDeg);
}

// _____________________________________________________________________________
void LineGraph::edgeDel(LineNode* n, const LineEdge* oldE) {
  // remove from from
  for (auto& r : n->pl().getConnExc()) {
    auto exFr = r.second.begin();
    while (exFr != r.second.end()) {
      if (exFr->first == oldE) {
        exFr = r.second.erase(exFr);
      } else {
        exFr++;
      }
    }
  }

  // remove from to
  for (auto& r : n->pl().getConnExc()) {
    for (auto& exFr : r.second) {
      auto exTo = exFr.second.begin();
      while (exTo != exFr.second.end()) {
        if (*exTo == oldE) {
          exTo = exFr.second.erase(exTo);
        } else {
          exTo++;
        }
      }
    }
  }
}

// _____________________________________________________________________________
void LineGraph::edgeRpl(LineNode* n, const LineEdge* oldE,
                        const LineEdge* newE) {
  if (oldE == newE) return;
  // replace in from
  for (auto& r : n->pl().getConnExc()) {
    auto exFr = r.second.begin();
    while (exFr != r.second.end()) {
      if (exFr->first == oldE) {
        std::swap(r.second[newE], exFr->second);
        exFr = r.second.erase(exFr);
      } else {
        exFr++;
      }
    }
  }

  // replace in to
  for (auto& r : n->pl().getConnExc()) {
    for (auto& exFr : r.second) {
      auto exTo = exFr.second.begin();
      while (exTo != exFr.second.end()) {
        if (*exTo == oldE) {
          exFr.second.insert(newE);
          exTo = exFr.second.erase(exTo);
        } else {
          exTo++;
        }
      }
    }
  }
}

// _____________________________________________________________________________
void LineGraph::nodeRpl(LineEdge* e, const LineNode* oldN,
                        const LineNode* newN) {
  auto ro = e->pl().getLines().begin();
  while (ro != e->pl().getLines().end()) {
    if (ro->direction == oldN) {
      shared::linegraph::LineOcc newRo = *ro;
      newRo.direction = newN;

      e->pl().updateLineOcc(newRo);
    }
    ro++;
  }
}

// _____________________________________________________________________________
void LineGraph::contractStrayNds() {
  std::vector<LineNode*> toDel;
  for (auto n : getNds()) {
    if (n->pl().stops().size()) continue;
    if (n->getAdjList().size() != 2) continue;

    auto eA = n->getAdjList().front();
    auto eB = n->getAdjList().back();

    // check if all lines continue over this node
    bool lineEqual = true;

    for (const auto& lo : eA->pl().getLines()) {
      if (!lineCtd(eA, eB, lo.line)) {
        lineEqual = false;
        break;
      }
    }

    if (!lineEqual) continue;

    for (const auto& lo : eB->pl().getLines()) {
      if (!lineCtd(eB, eA, lo.line)) {
        lineEqual = false;
        break;
      }
    }

    if (!lineEqual) continue;

    toDel.push_back(n);
  }

  for (auto n : toDel) {
    if (n->getAdjList().size() == 2) {
      auto a = n->getAdjList().front();
      auto b = n->getAdjList().back();

      // if this combination would turn our graph into a multigraph,
      // dont do it!
      if (getEdg(a->getOtherNd(n), b->getOtherNd(n))) continue;

      auto pl = a->pl();

      if (a->getTo() == n) {
        pl.setPolyline(PolyLine<double>(*a->getFrom()->pl().getGeom(),
                                        *b->getOtherNd(n)->pl().getGeom()));
        auto newE = addEdg(a->getFrom(), b->getOtherNd(n), pl);
        edgeRpl(a->getFrom(), b, newE);
        edgeRpl(b->getOtherNd(n), b, newE);
        edgeRpl(a->getFrom(), a, newE);
        edgeRpl(b->getOtherNd(n), a, newE);
      } else {
        pl.setPolyline(PolyLine<double>(*b->getOtherNd(n)->pl().getGeom(),
                                        *a->getTo()->pl().getGeom()));
        auto newE = addEdg(b->getOtherNd(n), a->getTo(), pl);
        edgeRpl(a->getTo(), b, newE);
        edgeRpl(b->getOtherNd(n), b, newE);
        edgeRpl(a->getTo(), a, newE);
        edgeRpl(b->getOtherNd(n), a, newE);
      }

      delNd(n);
    }
  }
}

// _____________________________________________________________________________
void LineGraph::contractEdge(LineEdge* e) {
  auto n1 = e->getFrom();
  auto n2 = e->getTo();
  auto otherP = n2->pl().getGeom();
  auto newGeom = DPoint((n1->pl().getGeom()->getX() + otherP->getX()) / 2,
                        (n1->pl().getGeom()->getY() + otherP->getY()) / 2);
  LineNode* n = 0;

  // n2 is always the target node
  auto n1Pl = n1->pl();

  if (e->getTo()->pl().stops().size() > 0) {
    auto servedLines = LineGraph::servedLines(e->getTo());
    n = mergeNds(e->getFrom(), e->getTo());

    for (auto l : LineGraph::servedLines(n)) {
      if (!servedLines.count(l)) n->pl().addLineNotServed(l);
    }
  } else if (e->getFrom()->pl().stops().size() > 0) {
    auto servedLines = LineGraph::servedLines(e->getFrom());
    n = mergeNds(e->getTo(), e->getFrom());

    for (auto l : LineGraph::servedLines(n)) {
      if (!servedLines.count(l)) n->pl().addLineNotServed(l);
    }
  } else {
    n = mergeNds(e->getTo(), e->getFrom());
  }

  n->pl().setGeom(newGeom);
}

// _____________________________________________________________________________
LineNode* LineGraph::mergeNds(LineNode* a, LineNode* b) {
  auto eConn = getEdg(a, b);

  std::vector<std::pair<const Line*, std::pair<LineNode*, LineNode*>>> ex;

  if (eConn) {
    for (auto fr : a->getAdjList()) {
      if (fr == eConn) continue;
      for (auto lo : fr->pl().getLines()) {
        for (auto to : b->getAdjList()) {
          if (to == eConn) continue;
          if (fr->pl().hasLine(lo.line) && to->pl().hasLine(lo.line) &&
              (!lineCtd(fr, eConn, lo.line) || !lineCtd(eConn, to, lo.line))) {
            ex.push_back({lo.line, {fr->getOtherNd(a), to->getOtherNd(b)}});
          }
        }
      }
    }

    edgeDel(a, eConn);
    edgeDel(b, eConn);
    _edgeGrid.remove(eConn);
    delEdg(a, b);
  }

  for (const auto& ex : a->pl().getConnExc()) {
    for (const auto& from : ex.second) {
      for (const auto& to : from.second) {
        b->pl().addConnExc(ex.first, from.first, to);
      }
    }
  }

  for (auto e : a->getAdjList()) {
    if (e->getFrom() != a) continue;
    if (e->getTo() == b) continue;
    auto ex = getEdg(b, e->getTo());  // check if edge already exists
    auto newE = addEdg(b, e->getTo(), e->pl());
    _edgeGrid.add(*newE->pl().getGeom(), newE);
    if (ex) {
      for (auto lo : e->pl().getLines()) {
        if (lo.direction == a) lo.direction = b;
        newE->pl().addLine(lo.line, lo.direction);
      }
    }
    edgeRpl(b, e, newE);
    edgeRpl(e->getTo(), e, newE);
    nodeRpl(newE, a, b);
  }

  for (auto e : a->getAdjList()) {
    if (e->getTo() != a) continue;
    if (e->getFrom() == b) continue;
    auto ex = getEdg(e->getFrom(), b);  // check if edge already exists
    auto newE = addEdg(e->getFrom(), b, e->pl());
    _edgeGrid.add(*newE->pl().getGeom(), newE);
    if (ex) {
      for (auto lo : e->pl().getLines()) {
        if (lo.direction == a) lo.direction = b;
        newE->pl().addLine(lo.line, lo.direction);
      }
    }
    edgeRpl(b, e, newE);
    edgeRpl(e->getFrom(), e, newE);
    nodeRpl(newE, a, b);
  }

  for (auto e : a->getAdjList()) _edgeGrid.remove(e);
  delNd(a);

  for (const auto& newEx : ex) {
    b->pl().addConnExc(newEx.first, getEdg(b, newEx.second.first),
                       getEdg(b, newEx.second.second));
  }

  return b;
}

// _____________________________________________________________________________
std::vector<Partner> LineGraph::getPartners(const LineNode* nd,
                                            const LineEdge* e,
                                            const LineOcc& lo) {
  std::vector<Partner> ret;
  for (auto toEdg : nd->getAdjList()) {
    if (toEdg == e) continue;

    for (const LineOcc& to : getCtdLinesIn(lo, e, toEdg)) {
      Partner p(toEdg, to.line);
      ret.push_back(p);
    }
  }
  return ret;
}

// _____________________________________________________________________________
void LineGraph::contractEdges(double d) { contractEdges(d, false); }

// _____________________________________________________________________________
void LineGraph::contractEdges(double d, bool onlyNonStatConns) {
  // TODO: the problem here is that contractEdge(e) may delete and replace edges
  // in the graph, which is why we cannot just build a list of edges eligible
  // for contraction and iterate over it - we would have to record the changes
  // made in contractEdge(e) and propagate it back.

breakfor:
  for (auto n1 : getNds()) {
    for (auto e : n1->getAdjList()) {
      if (e->getFrom() != n1) continue;
      if (onlyNonStatConns && (e->getFrom()->pl().stops().size() ||
                               e->getTo()->pl().stops().size()))
        continue;
      if (onlyNonStatConns && (e->getFrom()->pl().stops().size() ||
                               e->getTo()->pl().stops().size()))
        continue;
      if (!e->pl().dontContract() && e->pl().getPolyline().getLength() < d) {
        if (e->getOtherNd(n1)->getAdjList().size() > 1 &&
            n1->getAdjList().size() > 1 &&
            (n1->pl().stops().size() == 0 ||
             e->getOtherNd(n1)->pl().stops().size() == 0)) {
          contractEdge(e);
          goto breakfor;
        }
      }
    }
  }
}

// _____________________________________________________________________________
bool LineGraph::terminatesAt(const LineEdge* fromEdge, const LineNode* terminus,
                             const Line* line) {
  for (const auto& toEdg : terminus->getAdjList()) {
    if (toEdg == fromEdge) continue;

    if (lineCtd(fromEdge, toEdg, line)) {
      return false;
    }
  }

  return true;
}

// _____________________________________________________________________________
double LineGraph::searchSpaceSize() const {
  double ret = 1;

  for (auto n : getNds()) {
    for (auto e : n->getAdjList()) {
      if (e->getFrom() != n) continue;
      ret *= util::factorial(e->pl().getLines().size());
    }
  }

  return ret;
}

// _____________________________________________________________________________
size_t LineGraph::numConnExcs() const {
  size_t ret = 0;
  for (auto n : getNds()) ret += n->pl().numConnExcs();
  return ret;
}
