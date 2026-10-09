// Local implementation: scan conversion of filled paths for the CPU canvas.

#include "path_rasterizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <utility>

#include "path_geometry.h"

namespace bkit {

namespace {

// Maximum distance between a flattened curve and the curve, in pixels.
constexpr float kFlattenTolerance = 0.1f;
constexpr int kMaxCurveSegments = 256;

struct Edge {
  ScalarPoint p0;
  ScalarPoint p1;
};

float Length(ScalarPoint v) {
  return std::sqrt(v.x * v.x + v.y * v.y);
}

class Flattener {
public:
  explicit Flattener(std::vector<Edge>* edges)
      : edges_(edges) {
  }

  void MoveTo(ScalarPoint p) {
    CloseContour();
    start_ = current_ = p;
    open_ = true;
  }

  void LineTo(ScalarPoint p) {
    if (current_.x != p.x || current_.y != p.y) {
      edges_->push_back({current_, p});
    }
    current_ = p;
  }

  void QuadTo(ScalarPoint c, ScalarPoint e) {
    const ScalarPoint dd = {current_.x - 2 * c.x + e.x, current_.y - 2 * c.y + e.y};
    // The chord error of n uniform segments is |dd| / (8 n^2).
    const int n = SegmentCount(Length(dd) / (8 * kFlattenTolerance));
    const ScalarPoint s = current_;
    for (int i = 1; i <= n; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(n);
      const float mt = 1 - t;
      LineTo({mt * mt * s.x + 2 * mt * t * c.x + t * t * e.x,
              mt * mt * s.y + 2 * mt * t * c.y + t * t * e.y});
    }
    current_ = e;
  }

  void CubicTo(ScalarPoint c1, ScalarPoint c2, ScalarPoint e) {
    const ScalarPoint dd1 = {current_.x - 2 * c1.x + c2.x, current_.y - 2 * c1.y + c2.y};
    const ScalarPoint dd2 = {c1.x - 2 * c2.x + e.x, c1.y - 2 * c2.y + e.y};
    // The chord error of n uniform segments is at most 3 max|dd| / (4 n^2).
    const int n = SegmentCount(3 * std::max(Length(dd1), Length(dd2)) / (4 * kFlattenTolerance));
    const ScalarPoint s = current_;
    for (int i = 1; i <= n; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(n);
      const float mt = 1 - t;
      const float a = mt * mt * mt;
      const float b = 3 * mt * mt * t;
      const float c = 3 * mt * t * t;
      const float d = t * t * t;
      LineTo({a * s.x + b * c1.x + c * c2.x + d * e.x, a * s.y + b * c1.y + c * c2.y + d * e.y});
    }
    current_ = e;
  }

  void ConicTo(ScalarPoint control, ScalarPoint end, float weight) {
    const Conic conic(current_, control, end, weight);
    ScalarPoint quads[1 + 2 * (1 << Conic::kMaxConicToQuadPOW2)];
    const int count = conic.ChopIntoQuadsPOW2(quads, conic.ComputeQuadPOW2(kFlattenTolerance));
    for (int i = 0; i < count; ++i) {
      QuadTo(quads[2 * i + 1], quads[2 * i + 2]);
    }
  }

  void CloseContour() {
    if (open_) {
      LineTo(start_);
      open_ = false;
    }
  }

private:
  static int SegmentCount(float squared) {
    if (!(squared > 1)) {
      return 1;
    }
    const float n = std::ceil(std::sqrt(squared));
    return n >= static_cast<float>(kMaxCurveSegments) ? kMaxCurveSegments : static_cast<int>(n);
  }

  std::vector<Edge>* edges_;
  ScalarPoint start_;
  ScalarPoint current_;
  bool open_ = false;
};

void FlattenPath(const ScalarPath& path, const ScalarMatrix& matrix, std::vector<Edge>* edges) {
  Flattener flattener(edges);
  const auto points = path.Points();
  std::size_t index = 0;
  std::size_t conic_index = 0;
  const auto next = [&]() {
    return matrix.MapPoint(points[index++]);
  };
  for (ScalarPath::Verb verb : path.Verbs()) {
    switch (verb) {
    case ScalarPath::Verb::kMove:
      flattener.MoveTo(next());
      break;
    case ScalarPath::Verb::kLine:
      flattener.LineTo(next());
      break;
    case ScalarPath::Verb::kQuad: {
      ScalarPoint c = next();
      ScalarPoint e = next();
      flattener.QuadTo(c, e);
      break;
    }
    case ScalarPath::Verb::kConic: {
      ScalarPoint c = next();
      ScalarPoint e = next();
      flattener.ConicTo(c, e, path.ConicWeights()[conic_index++]);
      break;
    }
    case ScalarPath::Verb::kCubic: {
      ScalarPoint c1 = next();
      ScalarPoint c2 = next();
      ScalarPoint e = next();
      flattener.CubicTo(c1, c2, e);
      break;
    }
    case ScalarPath::Verb::kClose:
      flattener.CloseContour();
      break;
    }
  }
  flattener.CloseContour();
}

// Accumulates the area of already-resolved fill intervals in one pixel row.
// The prefix sum represents coverage, not winding: winding must be resolved
// before integration (as in SkScan_AAAPath's filled trapezoid intervals).
class RowAccumulator {
public:
  explicit RowAccumulator(int width)
      : width_(width), area_(static_cast<std::size_t>(width) + 2, 0.0) {
  }

  void Reset() {
    std::fill(area_.begin(), area_.end(), 0.0);
  }

  // The slab is contained in a single pixel row. Clip the boundary edge
  // into at most three pieces, including the vertical pieces outside the
  // mask. Strict interior split parameters make boundary endpoints final.
  void AddEdge(double x0, double x1, double signed_height) {
    double cuts[4] = {0, 1};
    int count = 2;
    if (x0 != x1) {
      for (double boundary : {0.0, static_cast<double>(width_)}) {
        const double t = (boundary - x0) / (x1 - x0);
        if (t > 0 && t < 1) {
          cuts[count++] = t;
        }
      }
    }
    std::sort(cuts, cuts + count);
    for (int i = 1; i < count; ++i) {
      const double a = std::clamp(std::lerp(x0, x1, cuts[i - 1]), 0.0, static_cast<double>(width_));
      const double b = std::clamp(std::lerp(x0, x1, cuts[i]), 0.0, static_cast<double>(width_));
      AddLine(a, b, signed_height * (cuts[i] - cuts[i - 1]));
    }
  }

  void Resolve(float* coverage) const {
    double accumulated = 0;
    for (int x = 0; x < width_; ++x) {
      accumulated += area_[static_cast<std::size_t>(x)];
      coverage[x] = static_cast<float>(std::clamp(accumulated, 0.0, 1.0));
    }
  }

private:
  void AddLine(double a, double b, double height) {
    if (height == 0) {
      return;
    }
    const double x0 = std::min(a, b);
    const double x1 = std::max(a, b);
    const int first = static_cast<int>(std::floor(x0));
    const int last = static_cast<int>(std::ceil(x1));
    if (last <= first + 1) {
      const double fraction = 0.5 * (a + b) - first;
      Add(first, height * (1 - fraction));
      Add(first + 1, height * fraction);
    } else {
      const double slope = 1 / (x1 - x0);
      const double first_fraction = x0 - first;
      const double first_area = 0.5 * slope * (1 - first_fraction) * (1 - first_fraction);
      const double last_fraction = x1 - last + 1;
      const double last_area = 0.5 * slope * last_fraction * last_fraction;
      Add(first, height * first_area);
      if (last == first + 2) {
        Add(first + 1, height * (1 - first_area - last_area));
      } else {
        const double second_area = slope * (1.5 - first_fraction);
        Add(first + 1, height * (second_area - first_area));
        for (int x = first + 2; x < last - 1; ++x) {
          Add(x, height * slope);
        }
        const double before_last_area = second_area + (last - first - 3) * slope;
        Add(last - 1, height * (1 - before_last_area - last_area));
      }
      Add(last, height * last_area);
    }
  }

  void Add(int x, double value) {
    area_[static_cast<std::size_t>(x)] += value;
  }

  int width_;
  std::vector<double> area_;
};

struct ScanEdge {
  double x;
  double top;
  double bottom;
  double slope;
  int winding;

  double X(double y) const {
    return x + slope * (y - top);
  }
};

struct ActiveEdge {
  const ScanEdge* edge;
  double x;
};

// At an intersection, round-off can put coincident edges a few double ULPs
// apart. Sort those groups by slope to obtain their order immediately below
// the event. Grouping after an exact sort keeps the comparator transitive.
void SortActiveEdges(double y, std::vector<ActiveEdge>* active) {
  for (ActiveEdge& entry : *active) {
    entry.x = entry.edge->X(y);
  }
  std::sort(active->begin(), active->end(), [](const ActiveEdge& a, const ActiveEdge& b) {
    return a.x < b.x;
  });
  for (std::size_t first = 0; first < active->size();) {
    const ActiveEdge& anchor = (*active)[first];
    const double tolerance = 8 * std::numeric_limits<double>::epsilon() *
                             (1 + std::abs(anchor.x) + std::abs(anchor.edge->x) +
                              std::abs(anchor.edge->slope * (y - anchor.edge->top)));
    std::size_t end = first + 1;
    while (end < active->size() && (*active)[end].x - anchor.x <= tolerance) {
      ++end;
    }
    std::sort(active->begin() + first, active->begin() + end, [](const ActiveEdge& a, const ActiveEdge& b) {
      return a.edge->slope < b.edge->slope;
    });
    first = end;
  }
}

void RasterizeAntiAliased(const std::vector<Edge>& edges, const IntRect& bounds, bool even_odd,
                         std::vector<float>* coverage) {
  const int width = bounds.Width();
  const int height = bounds.Height();
  coverage->assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);

  std::vector<ScanEdge> scan_edges;
  scan_edges.reserve(edges.size());
  for (const Edge& edge : edges) {
    if (edge.p0.y == edge.p1.y) {
      continue;
    }
    const bool down = edge.p0.y < edge.p1.y;
    const ScalarPoint p0 = down ? edge.p0 : edge.p1;
    const ScalarPoint p1 = down ? edge.p1 : edge.p0;
    const double top = std::max(static_cast<double>(p0.y), static_cast<double>(bounds.top));
    const double bottom = std::min(static_cast<double>(p1.y), static_cast<double>(bounds.bottom));
    if (top >= bottom) {
      continue;
    }
    const double slope = (static_cast<double>(p1.x) - p0.x) / (static_cast<double>(p1.y) - p0.y);
    const double x = p0.x + slope * (top - p0.y) - bounds.left;
    scan_edges.push_back({x, top - bounds.top, bottom - bounds.top, slope, down ? 1 : -1});
  }
  std::sort(scan_edges.begin(), scan_edges.end(), [](const ScanEdge& a, const ScanEdge& b) {
    return a.top < b.top;
  });

  std::vector<ActiveEdge> active;
  active.reserve(scan_edges.size());
  std::size_t next_edge = 0;
  RowAccumulator accumulator(width);
  for (int row = 0; row < height; ++row) {
    accumulator.Reset();
    double y = row;
    const double row_bottom = static_cast<double>(row) + 1;
    while (y < row_bottom) {
      std::erase_if(active, [y](const ActiveEdge& entry) { return entry.edge->bottom <= y; });
      while (next_edge < scan_edges.size() && scan_edges[next_edge].top <= y) {
        const ScanEdge* edge = &scan_edges[next_edge++];
        if (edge->bottom > y) {
          active.push_back({edge, 0});
        }
      }

      double next_y = row_bottom;
      if (next_edge < scan_edges.size()) {
        next_y = std::min(next_y, scan_edges[next_edge].top);
      }
      for (const ActiveEdge& entry : active) {
        next_y = std::min(next_y, entry.edge->bottom);
      }
      SortActiveEdges(y, &active);

      // Until the next vertex, only adjacent edges can be the first pair to
      // cross. Split there so every interval keeps its winding throughout
      // the slab, including for self-intersecting and overlapping contours.
      for (std::size_t i = 1; i < active.size(); ++i) {
        const ActiveEdge& left = active[i - 1];
        const ActiveEdge& right = active[i];
        const double closing_speed = left.edge->slope - right.edge->slope;
        if (closing_speed > 0) {
          const double intersection = y + (right.x - left.x) / closing_speed;
          if (intersection > y && intersection < next_y) {
            next_y = intersection;
          }
        }
      }

      int winding = 0;
      const ScanEdge* left = nullptr;
      for (const ActiveEdge& entry : active) {
        const bool was_inside = even_odd ? (winding & 1) != 0 : winding != 0;
        winding += entry.edge->winding;
        const bool inside = even_odd ? (winding & 1) != 0 : winding != 0;
        if (inside && !was_inside) {
          left = entry.edge;
        } else if (!inside && was_inside) {
          accumulator.AddEdge(left->X(y), left->X(next_y), next_y - y);
          accumulator.AddEdge(entry.edge->X(y), entry.edge->X(next_y), y - next_y);
        }
      }
      y = next_y;
    }
    accumulator.Resolve(coverage->data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(width));
  }
}

void RasterizeAliased(const std::vector<Edge>& edges, const IntRect& bounds, bool even_odd, std::vector<float>* coverage) {
  const int width = bounds.Width();
  const int height = bounds.Height();
  coverage->assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);

  struct Crossing {
    double x;
    int winding;
  };
  std::vector<Crossing> crossings;
  for (int y = 0; y < height; ++y) {
    const double yc = static_cast<double>(bounds.top) + y + 0.5;
    crossings.clear();
    for (const Edge& edge : edges) {
      const float y_min = std::min(edge.p0.y, edge.p1.y);
      const float y_max = std::max(edge.p0.y, edge.p1.y);
      if (yc < y_min || yc >= y_max) {
        continue;
      }
      // SkLineClipper::sect_with_horizontal uses double precision and pins
      // the intersection to its endpoints to avoid overflow and overshoot.
      const double x0 = edge.p0.x;
      const double x1 = edge.p1.x;
      const double y0 = edge.p0.y;
      const double y1 = edge.p1.y;
      const double x = std::clamp(x0 + (yc - y0) * (x1 - x0) / (y1 - y0),
                                  std::min(x0, x1), std::max(x0, x1));
      // SkLineClipper::ClipLine projects portions outside the horizontal
      // clip onto its sides, preserving their winding. Clamp the crossings
      // equivalently before sorting and before any integer conversion.
      crossings.push_back({std::clamp(x, static_cast<double>(bounds.left), static_cast<double>(bounds.right)),
                           edge.p1.y > edge.p0.y ? 1 : -1});
    }
    std::sort(crossings.begin(), crossings.end(), [](const Crossing& a, const Crossing& b) {
      return a.x < b.x;
    });

    int winding = 0;
    float* row = &(*coverage)[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)];
    for (std::size_t i = 0; i + 1 < crossings.size(); ++i) {
      winding += crossings[i].winding;
      const bool inside = even_odd ? (winding & 1) != 0 : winding != 0;
      if (!inside) {
        continue;
      }
      // Pixels whose centers are in [left, right).
      const double left = crossings[i].x - bounds.left;
      const double right = crossings[i + 1].x - bounds.left;
      const int first = static_cast<int>(std::ceil(left - 0.5));
      const int last = static_cast<int>(std::ceil(right - 0.5));
      for (int x = first; x < last; ++x) {
        row[x] = 1.0f;
      }
    }
  }
}

} // namespace

void RasterizePath(const ScalarPath& path, const ScalarMatrix& matrix, const IntRect& clip,
                   bool anti_alias, CoverageMask* mask) {
  mask->bounds = IntRect();
  mask->coverage.clear();
  if (clip.IsEmpty() || path.IsEmpty() || !matrix.IsFinite()) return;

  std::vector<Edge> edges;
  FlattenPath(path, matrix, &edges);
  if (edges.empty()) {
    return;
  }

  float min_x = std::numeric_limits<float>::infinity();
  float min_y = std::numeric_limits<float>::infinity();
  float max_x = -std::numeric_limits<float>::infinity();
  float max_y = -std::numeric_limits<float>::infinity();
  for (const Edge& edge : edges) {
    for (const ScalarPoint& p : {edge.p0, edge.p1}) {
      if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
        return;
      }
      min_x = std::min(min_x, p.x);
      min_y = std::min(min_y, p.y);
      max_x = std::max(max_x, p.x);
      max_y = std::max(max_y, p.y);
    }
  }

  const auto to_int = [](float v) {
    return static_cast<std::int32_t>(std::clamp<double>(v, -(1 << 29), 1 << 29));
  };
  IntRect bounds = IntRect::MakeLTRB(std::max(to_int(std::floor(min_x)), clip.left),
                                     std::max(to_int(std::floor(min_y)), clip.top),
                                     std::min(to_int(std::ceil(max_x)), clip.right),
                                     std::min(to_int(std::ceil(max_y)), clip.bottom));
  if (bounds.IsEmpty()) {
    return;
  }

  const bool even_odd = path.GetFillType() == ScalarPath::FillType::kEvenOdd;
  mask->bounds = bounds;
  if (anti_alias) {
    RasterizeAntiAliased(edges, bounds, even_odd, &mask->coverage);
  } else {
    RasterizeAliased(edges, bounds, even_odd, &mask->coverage);
  }
}

} // namespace bkit
