// Copyright 2017, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Authors: Patrick Brosi <brosi@informatik.uni-freiburg.de>

#include "shared/linegraph/Line.h"

using shared::linegraph::Line;

// _____________________________________________________________________________
const std::string& Line::id() const { return _id; }

// _____________________________________________________________________________
const std::string& Line::label() const { return _label; }

// _____________________________________________________________________________
const std::string& Line::color() const { return _color; }

const std::string& Line::mylabel() const { return _myLabel; }

const std::string& Line::backLabel() const {return _backLabel; }

const std::string& Line::from() const {return _from; }

const std::string& Line::to() const {return _to; }