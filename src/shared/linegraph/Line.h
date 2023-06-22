// Copyright 2017, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Authors: Patrick Brosi <brosi@informatik.uni-freiburg.de>

#ifndef SHARED_LINEGRAPH_LINE_H_
#define SHARED_LINEGRAPH_LINE_H_

#include <string>
#include <vector>

namespace shared {
namespace linegraph {

class Line {
 public:
  Line(const std::string& id, const std::string& label,
       const std::string& color)
      : _id(id), _label(label), _color(color) {}
  Line(const std::string& id, const std::string& label,
       const std::string& color, const std::string& myLabel, const std::string& backLabel,
       const std::string& from, const std::string& to)
      : _id(id), _label(label), _color(color), _myLabel(myLabel), _backLabel(backLabel), _from(from), _to(to) {}

  const std::string& id() const;
  const std::string& label() const;
  const std::string& color() const;
  const std::string& mylabel() const;
  const std::string& backLabel() const;
  const std::string& from() const;
  const std::string& to() const;

 private:
  std::string _id, _label, _color, _myLabel, _backLabel, _from, _to;
};
}
}

#endif  // SHARED_LINEGRAPH_LINE_H_
