// Copyright 2016, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Authors: Patrick Brosi <brosi@informatik.uni-freiburg.de>

#include "transitmap/optim/OptGraph.h"

using namespace transitmapper;
using namespace optim;

// _____________________________________________________________________________
EtgPart OptEdge::getFirstEdge() const {
  for (const auto e : etgs) {
    if (e.etg->getFrom() == from->node || e.etg->getTo() == from->node) {
      return e;
    }
  }
}

// _____________________________________________________________________________
EtgPart OptEdge::getLastEdge() const {
  for (const auto e : etgs) {
    if (e.etg->getFrom() == to->node || e.etg->getTo() == to->node) {
      return e;
    }
  }
}

// _____________________________________________________________________________
std::string OptEdge::getStrRepr() const {
  const void* address = static_cast<const void*>(this);
  std::stringstream ss;
  ss << address;

  return ss.str();
}

// _____________________________________________________________________________
graph::Edge* OptEdge::getAdjacentEdge(const OptNode* n) const {
  if (from == n) {
    return getFirstEdge().etg;
  } else {
    return getLastEdge().etg;
  }
}

// _____________________________________________________________________________
OptGraph::OptGraph(TransitGraph* toOptim)
 : _g(toOptim) { build(); }

// _____________________________________________________________________________
void OptGraph::addNode(OptNode* n) { _nodes.insert(n); }

// _____________________________________________________________________________
void OptGraph::build() {
  for (graph::Node* n : *_g->getNodes()) {
    for (graph::Edge* e : n->getAdjListOut()) {
      graph::Node* fromTn = e->getFrom();
      graph::Node* toTn = e->getTo();

      OptNode* from = getNodeForTransitNode(fromTn);
      OptNode* to = getNodeForTransitNode(toTn);

      if (!from) {
        from = new OptNode(fromTn);
        addNode(from);
      }

      if (!to) {
        to = new OptNode(toTn);
        addNode(to);
      }

      OptEdge* edge = new OptEdge(from, to);
      from->addEdge(edge);
      to->addEdge(edge);

      edge->etgs.push_back(EtgPart(e, e->getTo() == toTn));
    }
  }
}

// _____________________________________________________________________________
const std::set<OptNode*>& OptGraph::getNodes() const { return _nodes; }

// _____________________________________________________________________________
OptNode* OptGraph::getNodeForTransitNode(const Node* tn) const {
  for (auto n : _nodes) {
    if (n->node == tn) return n;
  }

  return 0;
}

// _____________________________________________________________________________
void OptGraph::simplify() {
  while (simplifyStep()) {
  }
}

// _____________________________________________________________________________
bool OptGraph::simplifyStep() {
  for (OptNode* n : _nodes) {
    if (n->adjList.size() == 2) {
      OptEdge* first = 0;
      OptEdge* second = 0;

      for (OptEdge* e : n->adjList) {
        if (!first)
          first = e;
        else
          second = e;
      }

      bool equal = first->etgs.front().etg->getCardinality() ==
                   second->etgs.front().etg->getCardinality();

      for (auto& to : *first->getAdjacentEdge(n)->getTripsUnordered()) {
        if (!second->getAdjacentEdge(n)
                 ->getSameDirRoutesIn(n->node, to.route, to.direction,
                                      first->getAdjacentEdge(n))
                 .size()) {
          equal = false;
          break;
        }
      }

      if (equal) {
        OptNode* newFrom = 0;
        OptNode* newTo = 0;

        bool firstReverted;
        bool secondReverted;

        // add new edge
        if (first->to != n) {
          newFrom = first->to;
          firstReverted = true;
        } else {
          newFrom = first->from;
          firstReverted = false;
        }

        if (second->to != n) {
          newTo = second->to;
          secondReverted = false;
        } else {
          newTo = second->from;
          secondReverted = true;
        }

        if (newFrom == newTo) continue;

        OptEdge* newEdge = new OptEdge(newFrom, newTo);

        // add etgs...
        for (EtgPart& etgp : first->etgs) {
          newEdge->etgs.push_back(
              EtgPart(etgp.etg, (etgp.dir ^ firstReverted)));
        }
        for (EtgPart& etgp : second->etgs) {
          newEdge->etgs.push_back(
              EtgPart(etgp.etg, (etgp.dir ^ secondReverted)));
        }

        assert(newFrom != n);
        assert(newTo != n);

        delete (n);
        _nodes.erase(n);

        newFrom->deleteEdge(first);
        newFrom->deleteEdge(second);
        newTo->deleteEdge(first);
        newTo->deleteEdge(second);

        newFrom->addEdge(newEdge);
        newTo->addEdge(newEdge);
        return true;
      }
    }
  }
  return false;
}

// _____________________________________________________________________________
TransitGraph* OptGraph::getGraph() const { return _g; }

// _____________________________________________________________________________
size_t OptGraph::getNumNodes() const { return _nodes.size(); }

// _____________________________________________________________________________
size_t OptGraph::getNumNodes(bool topo) const {
  size_t ret = 0;
  for (auto n : _nodes) {
    if ((n->node->getStops().size() == 0) ^ !topo) ret++;
  }

  return ret;
}

// _____________________________________________________________________________
size_t OptGraph::getNumEdges() const {
  size_t ret = 0;

  for (auto n : getNodes()) {
    ret += n->adjListOut.size();
  }

  return ret;
}

// _____________________________________________________________________________
size_t OptGraph::getNumRoutes() const {
  std::set<const graph::Route*> routes;

  for (auto n : getNodes()) {
    for (auto e : n->adjListOut) {
      for (const auto& to : *e->getFirstEdge().etg->getTripsUnordered()) {
        if (to.route->relativeTo()) continue;
        routes.insert(to.route);
      }
    }
  }
  return routes.size();
}

// _____________________________________________________________________________
size_t OptGraph::getMaxCardinality() const {
  size_t ret = 0;
  for (auto n : getNodes()) {
    for (auto e : n->adjListOut) {
      if (e->getFirstEdge().etg->getCardinality(true) > ret) {
        ret = e->getFirstEdge().etg->getCardinality(true);
      }
    }
  }

  return ret;
}
