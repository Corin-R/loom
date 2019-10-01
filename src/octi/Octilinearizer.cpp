// Copyright 2017, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Authors: Patrick Brosi <brosi@informatik.uni-freiburg.de>

#include <fstream>
#include "octi/Octilinearizer.h"
#include "octi/combgraph/Drawing.h"
#include "octi/gridgraph/NodeCost.h"
#include "util/Misc.h"
#include "util/geo/output/GeoGraphJsonOutput.h"
#include "util/graph/Dijkstra.h"
#include "util/log/Log.h"

#include "ilp/ILPGridOptimizer.h"

using namespace octi;
using namespace gridgraph;

using octi::combgraph::Drawing;
using combgraph::EdgeOrdering;
using util::graph::Dijkstra;
using util::graph::Dijkstra;
using util::geo::len;
using util::geo::dist;
using util::geo::DPoint;

// _____________________________________________________________________________
void Octilinearizer::removeEdgesShorterThan(TransitGraph* g, double d) {
start:
  for (auto n1 : *g->getNds()) {
    for (auto e1 : n1->getAdjList()) {
      if (e1->pl().getPolyline().getLength() < d) {
        if (e1->getOtherNd(n1)->getAdjList().size() > 1 &&
            n1->getAdjList().size() > 1 &&
            (n1->pl().getStops().size() == 0 ||
             e1->getOtherNd(n1)->pl().getStops().size() == 0)) {
          auto otherP = e1->getFrom()->pl().getGeom();

          TransitNode* n = 0;

          if (e1->getTo()->pl().getStops().size() > 0) {
            n = g->mergeNds(e1->getFrom(), e1->getTo());
          } else {
            n = g->mergeNds(e1->getTo(), e1->getFrom());
          }

          n->pl().setGeom(
              DPoint((n->pl().getGeom()->getX() + otherP->getX()) / 2,
                     (n->pl().getGeom()->getY() + otherP->getY()) / 2));
          goto start;
        }
      }
    }
  }
}

// _____________________________________________________________________________
double Octilinearizer::getMaxDis(CombNode* to, CombEdge* e, double gridSize) {
  return gridSize * 3;
}

// _____________________________________________________________________________
TransitGraph Octilinearizer::draw(TransitGraph* tg, GridGraph** retGg,
                                  const Penalties& pens, double gridSize,
                                  double borderRad) {
  std::cerr << "Removing short edges... ";
  T_START(remshortegs);
  removeEdgesShorterThan(tg, gridSize / 2);
  std::cerr << " done (" << T_STOP(remshortegs) << "ms)" << std::endl;

  std::cerr << "Building combination graph... ";
  T_START(combgraph);
  CombGraph cg(tg);
  std::cerr << " done (" << T_STOP(combgraph) << "ms)" << std::endl;

  auto box = tg->getBBox();

  T_START(grid);
  auto gg = new GridGraph(box, gridSize, borderRad, pens);

  ///////////
  ilp::ILPGridOptimizer ilpoptim;

  ilpoptim.optimize(gg, cg);

  util::geo::output::GeoGraphJsonOutput out;
  std::ofstream of;
  of.open("octi.json");
  out.print(*gg, of);
  of << std::flush;

  exit(0);

  /////////////

  std::cerr << "Build grid graph in " << T_STOP(grid) << " ms " << std::endl;

  auto order = getOrdering(cg);

  Drawing drawing(gg);
  bool found = draw(order, gg, &drawing);
  double origScore = drawing.score();

  if (!found) std::cerr << "(no initial embedding found)" << std::endl;

  Drawing bestIterDraw = drawing;
  drawing.eraseFromGrid(gg);
  bool iterFound = false;
  for (size_t i = 0; i < 10; i++) {
    auto iterOrder = getOrdering(cg);

    Drawing nextDrawing(gg);
    bool locFound = draw(iterOrder, gg, &nextDrawing);

    if (locFound) {
      double imp = (bestIterDraw.score() - nextDrawing.score());
      std::cerr << " ++ Iter " << i << ", prev " << bestIterDraw.score()
                << ", next " << nextDrawing.score() << " ("
                << (imp >= 0 ? "+" : "") << imp << ")" << std::endl;

      if (!iterFound || nextDrawing.score() < bestIterDraw.score()) {
        bestIterDraw = nextDrawing;
        iterFound = true;
      }
    }

    nextDrawing.eraseFromGrid(gg);
  }

  if (iterFound) {
    drawing = bestIterDraw;
    found = true;
  }

  if (!found) {
    LOG(ERROR) << "Could not find planar embedding for input graph.";
    exit(1);
  }

  drawing.applyToGrid(gg);

  size_t iters = 0;

  for (; iters < 100; iters++) {
    Drawing bestFromIter = drawing;
    for (auto a : *cg.getNds()) {
      if (a->getDeg() == 0) continue;
      assert(drawing.getGrNd(a));

      Drawing drawingCp = drawing;
      size_t origX = drawing.getGrNd(a)->pl().getX();
      size_t origY = drawing.getGrNd(a)->pl().getY();

      // reverting a
      std::vector<CombEdge*> test;
      for (auto ce : a->getAdjList()) {
        assert(drawingCp.drawn(ce));
        test.push_back(ce);

        drawingCp.eraseFromGrid(ce, gg);
        drawingCp.erase(ce);
      }

      drawingCp.erase(a);
      gg->unSettleNd(a);

      for (size_t pos = 0; pos < 9; pos++) {
        Drawing run = drawingCp;
        SettledPos p;

        if (pos == 1) p[a] = {origX + 1, origY + 1};
        if (pos == 2) p[a] = {origX + 1, origY};
        if (pos == 3) p[a] = {origX + 1, origY - 1};

        if (pos == 7) p[a] = {origX - 1, origY + 1};
        if (pos == 6) p[a] = {origX - 1, origY};
        if (pos == 5) p[a] = {origX - 1, origY - 1};

        if (pos == 0) p[a] = {origX, origY + 1};
        if (pos == 8) p[a] = {origX, origY};
        if (pos == 4) p[a] = {origX, origY - 1};

        bool found = draw(test, p, gg, &run);

        if (found && bestFromIter.score() > run.score()) {
          bestFromIter = run;
        }

        // reset grid
        for (auto ce : a->getAdjList()) run.eraseFromGrid(ce, gg);

        if (gg->isSettled(a)) gg->unSettleNd(a);
      }

      gg->settleNd(const_cast<GridNode*>(drawing.getGrNd(a)), a);

      // RE-SETTLE EDGES!
      for (auto ce : a->getAdjList()) drawing.applyToGrid(ce, gg);
    }

    double imp = (drawing.score() - bestFromIter.score());
    std::cerr << " ++ Iter " << iters << ", prev " << drawing.score()
              << ", next " << bestFromIter.score() << " ("
              << (imp >= 0 ? "+" : "") << imp << ")" << std::endl;

    if (imp < 0.05) break;

    drawing.eraseFromGrid(gg);
    bestFromIter.applyToGrid(gg);
    drawing = bestFromIter;
  }

  TransitGraph ret;
  drawing.getTransitGraph(&ret);

  *retGg = gg;

  return ret;
}

// _____________________________________________________________________________
void Octilinearizer::settleRes(GridNode* frGrNd, GridNode* toGrNd,
                               GridGraph* gg, CombNode* from, CombNode* to,
                               const GrEdgList& res, CombEdge* e) {
  gg->settleNd(toGrNd, to);
  gg->settleNd(frGrNd, from);

  // balance edges
  for (auto f : res) {
    if (f->pl().isSecondary()) continue;
    gg->settleEdg(f->getFrom()->pl().getParent(), f->getTo()->pl().getParent(),
                  e);
  }
}

// _____________________________________________________________________________
void Octilinearizer::writeNdCosts(GridNode* n, CombNode* origNode, CombEdge* e,
                                  GridGraph* g) {
  NodeCost c;
  c += g->topoBlockPenalty(n, origNode, e);
  c += g->nodeBendPenalty(n, e);

  g->addCostVector(n, c);
}

// _____________________________________________________________________________
bool Octilinearizer::draw(const std::vector<CombEdge*>& order, GridGraph* gg,
                          Drawing* drawing) {
  SettledPos emptyPos;
  return draw(order, emptyPos, gg, drawing);
}

// _____________________________________________________________________________
bool Octilinearizer::draw(const std::vector<CombEdge*>& ord,
                          const SettledPos& settled, GridGraph* gg,
                          Drawing* drawing) {
  bool fail = false;

  double c_0 = gg->getPenalties().p_45 - gg->getPenalties().p_135;

  SettledPos retPos;

  for (auto cmbEdg : ord) {
    auto frCmbNd = cmbEdg->getFrom();
    auto toCmbNd = cmbEdg->getTo();

    bool reversed = false;

    if ((!gg->isSettled(frCmbNd) && gg->isSettled(toCmbNd))) {
      auto tmp = frCmbNd;
      frCmbNd = toCmbNd;
      toCmbNd = tmp;
      reversed = !reversed;
    }

    GridNode* frGrNd;
    std::set<GridNode*> toGrNds;

    // STEP 1
    // grid node selection
    if (gg->getSettled(frCmbNd)) {
      frGrNd = gg->getSettled(frCmbNd);
    } else if (settled.count(frCmbNd)) {
      frGrNd = gg->getNode(settled.find(frCmbNd)->second.first,
                           settled.find(frCmbNd)->second.second);
      if (!frGrNd || frGrNd->pl().isClosed()) {
        fail = true;
        break;
      }
    } else {
      frGrNd = gg->getGridNodeFrom(frCmbNd, gg->getCellSize() * 1.7, 0);
    }

    if (gg->getSettled(toCmbNd)) {
      toGrNds.insert(gg->getSettled(toCmbNd));
    } else if (settled.count(toCmbNd)) {
      toGrNds.insert(gg->getNode(settled.find(toCmbNd)->second.first,
                                 settled.find(toCmbNd)->second.second));
      if (!(*toGrNds.begin()) || *toGrNds.begin() == frGrNd ||
          (*toGrNds.begin())->pl().isClosed()) {
        fail = true;
        break;
      }
    } else {
      // get surrounding displacement nodes
      double maxDis = getMaxDis(toCmbNd, cmbEdg, gg->getCellSize());
      toGrNds = gg->getGridNodesTo(toCmbNd, maxDis, frGrNd);

      // TODO: abort criteria
      while (!toGrNds.size()) {
        maxDis *= 2;
        toGrNds = gg->getGridNodesTo(toCmbNd, maxDis, frGrNd);
      }
    }

    if (!frGrNd || toGrNds.size() == 0) {
      fail = true;
      break;
    }

    for (auto to : toGrNds) assert(to != frGrNd);

    // END STEP 1

    // why not distance based? (TODO, balance this with edge costs)
    double penPerGrid = 5 + c_0 + fmax(gg->getPenalties().diagonalPen,
                                       gg->getPenalties().horizontalPen);

    // open the target nodes
    for (auto n : toGrNds) {
      double gridD = floor(dist(*n->pl().getGeom(), *toCmbNd->pl().getGeom()));
      gridD = gridD / gg->getCellSize();

      if (gg->isSettled(toCmbNd)) {
        // only count displacement penalty ONCE
        gg->openNodeSink(n, 0);
      } else {
        gg->openNodeSink(n, gridD * penPerGrid);
      }
    }

    // open from source node
    if (gg->isSettled(frCmbNd)) {
      // only count displacement penalty ONCE
      gg->openNodeSink(frGrNd, 0);
    } else {
      double gridD =
          floor(dist(*frGrNd->pl().getGeom(), *frCmbNd->pl().getGeom()));
      gridD = gridD / gg->getCellSize();
      gg->openNodeSink(frGrNd, gridD * penPerGrid);
    }

    if (gg->isSettled(frCmbNd)) {
      writeNdCosts(frGrNd, frCmbNd, cmbEdg, gg);
    }

    if (toGrNds.size() == 1 && gg->isSettled(toCmbNd)) {
      // the size() == 1 check is important, because nd cost writing will
      // not work if the to node is not already settled!
      writeNdCosts(*toGrNds.begin(), toCmbNd, cmbEdg, gg);
    }

    GrEdgList eL;
    GrNdList nL;
    GridNode* toGrNd = 0;
    Dijkstra::shortestPath(frGrNd, toGrNds, GridCost(),
                           GridHeur(gg, frGrNd, toGrNds), &eL, &nL);

    if (!nL.size()) {
      // cleanup
      for (auto n : toGrNds) gg->closeNodeSink(n);
      gg->closeNodeSink(frGrNd);

      fail = true;
      break;
    }

    toGrNd = nL.front();

    // draw
    drawing->draw(cmbEdg, eL, reversed);

    // close the target node
    for (auto n : toGrNds) gg->closeNodeSink(n);

    // close the start node
    gg->closeNodeSink(frGrNd);

    settleRes(frGrNd, toGrNd, gg, frCmbNd, toCmbNd, eL, cmbEdg);
  }

  return !fail;
}

// _____________________________________________________________________________
std::vector<CombEdge*> Octilinearizer::getOrdering(const CombGraph& cg) const {
  NodePQ globalPq, dangling;

  std::set<CombNode*> settled;
  std::vector<CombEdge*> order;

  for (auto n : cg.getNds()) globalPq.push(n);
  std::set<CombEdge*> done;

  while (!globalPq.empty()) {
    auto n = globalPq.top();
    globalPq.pop();
    dangling.push(n);

    while (!dangling.empty()) {
      auto n = dangling.top();
      dangling.pop();

      if (settled.find(n) != settled.end()) continue;

      auto odSet = n->pl().getEdgeOrdering().getOrderedSet();
      std::random_shuffle(odSet.begin(), odSet.end());

      for (auto ee : odSet) {
        if (done.find(ee.first) != done.end()) continue;
        done.insert(ee.first);
        dangling.push(ee.first->getOtherNd(n));

        order.push_back(ee.first);
      }
      settled.insert(n);
    }
  }

  return order;
}
