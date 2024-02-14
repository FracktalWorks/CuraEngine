// Copyright (c) 2024 UltiMaker
// CuraEngine is released under the terms of the AGPLv3 or higher.

#include "utils/polygons.h"

#include <unordered_set>

#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/io/wkt/read.hpp>
#include <fmt/format.h>
#include <range/v3/range/primitives.hpp>
#include <range/v3/to_container.hpp>
#include <range/v3/view/c_str.hpp>
#include <range/v3/view/concat.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/sliding.hpp>
#include <range/v3/view/take.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>
#include <spdlog/spdlog.h>

#include "utils/OpenPolylineStitcher.h"

namespace cura
{

Polygons Polygon::intersection(const Polygon& other) const
{
    Polygons ret;
    ClipperLib::Clipper clipper(clipper_init);
    clipper.AddPath(*this, ClipperLib::ptSubject, true);
    clipper.AddPath(other, ClipperLib::ptClip, true);
    clipper.Execute(ClipperLib::ctIntersection, ret.getCallable());
    return ret;
}

Polygons Polygons::approxConvexHull(int extra_outset) const
{
    constexpr int overshoot = MM2INT(100); // 10cm (hard-coded value).

    Polygons convex_hull;
    // Perform the offset for each polygon one at a time.
    // This is necessary because the polygons may overlap, in which case the offset could end up in an infinite loop.
    // See http://www.angusj.com/delphi/clipper/documentation/Docs/Units/ClipperLib/Classes/ClipperOffset/_Body.htm
    for (const path_t& path : (*this))
    {
        Polygons offset_result;
        ClipperLib::ClipperOffset offsetter(1.2, 10.0);
        offsetter.AddPath(path, ClipperLib::jtRound, ClipperLib::etClosedPolygon);
        offsetter.Execute(offset_result.getCallable(), overshoot);
        convex_hull.add(offset_result);
    }

    return convex_hull.unionPolygons().offset(-overshoot + extra_outset, ClipperLib::jtRound);
}

void Polygons::makeConvex()
{
    // early out if there is nothing to do
    if (empty())
    {
        return;
    }

    // Andrew’s Monotone Chain Convex Hull Algorithm
    std::vector<point_t> points;
    for (const path_t& poly : (*this))
    {
        points.insert(points.end(), poly.begin(), poly.end());
    }

    Polygon convexified;
    auto make_sorted_poly_convex = [&convexified](std::vector<point_t>& poly)
    {
        convexified.push_back(poly[0]);

        for (const auto window : poly | ranges::views::sliding(2))
        {
            const point_t& current = window[0];
            const point_t& after = window[1];

            if (LinearAlg2D::pointIsLeftOfLine(current, convexified.back(), after) < 0)
            {
                // Track backwards to make sure we haven't been in a concave pocket for multiple vertices already.
                while (convexified.size() >= 2
                       && (LinearAlg2D::pointIsLeftOfLine(convexified.back(), convexified[convexified.size() - 2], current) >= 0
                           || LinearAlg2D::pointIsLeftOfLine(convexified.back(), convexified[convexified.size() - 2], convexified.front()) > 0))
                {
                    convexified.pop_back();
                }
                convexified.push_back(current);
            }
        }
    };

    std::sort(
        points.begin(),
        points.end(),
        [](point_t a, point_t b)
        {
            return a.X == b.X ? a.Y < b.Y : a.X < b.X;
        });
    make_sorted_poly_convex(points);
    std::reverse(points.begin(), points.end());
    make_sorted_poly_convex(points);

    *this = { convexified };
}

Polygons Polygons::difference(const Polygons& other) const
{
    Polygons ret;
    ClipperLib::Clipper clipper(clipper_init);
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, true);
    clipper.AddPaths(other.getCallable(), ClipperLib::ptClip, true);
    clipper.Execute(ClipperLib::ctDifference, ret.getCallable());
    return ret;
}

Polygons Polygons::unionPolygons(const Polygons& other, ClipperLib::PolyFillType fill_type) const
{
    Polygons ret;
    ClipperLib::Clipper clipper(clipper_init);
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, true);
    clipper.AddPaths(other.getCallable(), ClipperLib::ptSubject, true);
    clipper.Execute(ClipperLib::ctUnion, ret.getCallable(), fill_type, fill_type);
    return ret;
}

Polygons Polygons::intersection(const Polygons& other) const
{
    Polygons ret;
    ClipperLib::Clipper clipper(clipper_init);
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, true);
    clipper.AddPaths(other.getCallable(), ClipperLib::ptClip, true);
    clipper.Execute(ClipperLib::ctIntersection, ret.getCallable());
    return ret;
}

Polygons& Polygons::operator=(Polygons&& polygons)
{
    if (this != &polygons)
    {
        (*this) = std::move(polygons);
    }
    return *this;
}

size_t Polygons::pointCount() const
{
    return std::accumulate(
        begin(),
        end(),
        size_t(0),
        [](size_t total, const Polygon& polygon)
        {
            return total + polygon.size();
        });
}

void Polygons::remove(size_t index)
{
    if (size() == 1)
    {
        clear();
    }
    else if (size() > 1)
    {
        POLY_ASSERT(index < size());
        if (index < size() - 1)
        {
            (*this)[index] = std::move(back());
        }
        resize(size() - 1);
    }
}

void Polygons::add(const Polygons& other)
{
    std::copy(other.begin(), other.end(), std::back_inserter(*this));
}

void Polygons::addIfNotEmpty(const Polygon& polygon)
{
    if (! polygon.empty())
    {
        push_back(polygon);
    }
}

void Polygons::addIfNotEmpty(Polygon&& polygon)
{
    if (! polygon.empty())
    {
        emplace_back(std::move(polygon));
    }
}

void Polygons::addLine(const point_t& from, const point_t& to)
{
    emplace_back(Polygon{ from, to });
}

Polygon& Polygons::newPoly()
{
    emplace_back();
    return back();
}

bool Polygons::inside(Point2LL p, bool border_result) const
{
    int poly_count_inside = 0;
    for (const ClipperLib::Path& poly : *this)
    {
        const int is_inside_this_poly = ClipperLib::PointInPolygon(p, poly);
        if (is_inside_this_poly == -1)
        {
            return border_result;
        }
        poly_count_inside += is_inside_this_poly;
    }
    return (poly_count_inside % 2) == 1;
}

bool PolygonsPart::inside(Point2LL p, bool border_result) const
{
    if (size() < 1)
    {
        return false;
    }

    if (! (*this)[0].inside(p, border_result))
    {
        return false;
    }

    for (unsigned int n = 1; n < size(); n++)
    {
        if ((*this)[n].inside(p, border_result))
        {
            return false;
        }
    }
    return true;
}

bool Polygons::insideOld(Point2LL p, bool border_result) const
{
    const Polygons& thiss = *this;
    if (size() < 1)
    {
        return false;
    }

    int crossings = 0;
    for (const ClipperLib::Path& poly : thiss)
    {
        Point2LL p0 = poly.back();
        for (const Point2LL& p1 : poly)
        {
            short comp = LinearAlg2D::pointLiesOnTheRightOfLine(p, p0, p1);
            if (comp == 1)
            {
                crossings++;
            }
            else if (comp == 0)
            {
                return border_result;
            }
            p0 = p1;
        }
    }
    return (crossings % 2) == 1;
}

size_t Polygons::findInside(Point2LL p, bool border_result) const
{
    if (size() < 1)
    {
        return false;
    }

    // NOTE: Keep these vectors fixed-size, they replace an (non-standard, sized at runtime) arrays.
    std::vector<int64_t> min_x(size(), std::numeric_limits<int64_t>::max());
    std::vector<int64_t> crossings(size());

    for (size_t poly_idx = 0; poly_idx < size(); poly_idx++)
    {
        const Polygon poly = (*this)[poly_idx];
        Point2LL p0 = poly.back();
        for (const Point2LL& p1 : poly)
        {
            short comp = LinearAlg2D::pointLiesOnTheRightOfLine(p, p0, p1);
            if (comp == 1)
            {
                crossings[poly_idx]++;
                int64_t x;
                if (p1.Y == p0.Y)
                {
                    x = p0.X;
                }
                else
                {
                    x = p0.X + (p1.X - p0.X) * (p.Y - p0.Y) / (p1.Y - p0.Y);
                }
                if (x < min_x[poly_idx])
                {
                    min_x[poly_idx] = x;
                }
            }
            else if (border_result && comp == 0)
            {
                return poly_idx;
            }
            p0 = p1;
        }
    }

    int64_t min_x_uneven = std::numeric_limits<int64_t>::max();
    size_t ret = NO_INDEX;
    size_t n_unevens = 0;
    for (size_t array_idx = 0; array_idx < size(); array_idx++)
    {
        if (crossings[array_idx] % 2 == 1)
        {
            n_unevens++;
            if (min_x[array_idx] < min_x_uneven)
            {
                min_x_uneven = min_x[array_idx];
                ret = array_idx;
            }
        }
    }
    if (n_unevens % 2 == 0)
    {
        ret = NO_INDEX;
    }
    return ret;
}

std::vector<OpenPolyline> Polygons::intersectionPolyLines(const LinesSet<OpenPolyline>& polylines, bool restitch, const coord_t max_stitch_distance) const
{
    LinesSet<OpenPolyline> split_polylines = polylines.splitIntoSegments();

    ClipperLib::PolyTree result;
    ClipperLib::Clipper clipper(clipper_init);
    clipper.AddPaths(split_polylines.getCallable(), ClipperLib::ptSubject, false);
    clipper.AddPaths(getCallable(), ClipperLib::ptClip, true);
    clipper.Execute(ClipperLib::ctIntersection, result);
    LinesSet<OpenPolyline> ret;
    ClipperLib::OpenPathsFromPolyTree(result, ret.getCallable());

    if (restitch)
    {
        std::vector<OpenPolyline> result_lines;
        Polygons result_polygons;
        const coord_t snap_distance = 10_mu;
        OpenPolylineStitcher::stitch(ret, result_lines, result_polygons, max_stitch_distance, snap_distance);
        ret = std::move(result_lines);
        // if polylines got stitched into polygons, split them back up into a polyline again, because the result only admits polylines
        for (const Polygon& poly : result_polygons)
        {
            if (! poly.empty())
            {
                ret.push_back(poly);
            }
        }
    }

    return ret;
}

Polygons Polygons::xorPolygons(const Polygons& other, ClipperLib::PolyFillType pft) const
{
    Polygons ret;
    ClipperLib::Clipper clipper(clipper_init);
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, true);
    clipper.AddPaths(other.getCallable(), ClipperLib::ptClip, true);
    clipper.Execute(ClipperLib::ctXor, ret.getCallable(), pft);
    return ret;
}

Polygons Polygons::execute(ClipperLib::PolyFillType pft) const
{
    Polygons ret;
    ClipperLib::Clipper clipper(clipper_init);
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, true);
    clipper.Execute(ClipperLib::ctXor, ret.getCallable(), pft);
    return ret;
}
/*
void Polygons::toPolylines()
{
    for (PolygonRef poly : *this)
    {
        if (poly.empty())
            continue;
        poly.emplace_back(poly.front());
    }
}

void Polygons::splitPolylinesIntoSegments(Polygons& result) const
{
for (ConstPolygonRef poly : *this)
{
  poly.splitPolylineIntoSegments(result);
 }
}
Polygons Polygons::splitPolylinesIntoSegments() const
{
 Polygons ret;
    splitPolylinesIntoSegments(ret);
    return ret;
}

void Polygons::splitPolygonsIntoSegments(Polygons& result) const
{
    for (ConstPolygonRef poly : *this)
    {
        poly.splitPolygonIntoSegments(result);
    }
}
Polygons Polygons::splitPolygonsIntoSegments() const
{
    Polygons ret;
    splitPolygonsIntoSegments(ret);
    return ret;
}

coord_t Polygons::polyLineLength() const
{
    coord_t length = 0;
    for (ConstPolygonRef poly : *this)
    {
        length += poly.polylineLength();
    }
    return length;
}
*/

template<class LineType>
Polygons LinesSet<LineType>::offset(coord_t distance, ClipperLib::JoinType joinType, double miter_limit) const
{
    if (distance == 0)
    {
        return Polygons(getCallable());
    }

    Polygons temp;
    const paths_t* actual_polygons = &getCallable();
    Polygons ret;
    ClipperLib::EndType end_type;
    if constexpr (LineType::shape_type_ == ShapeType::Filled)
    {
        temp = Polygons(getCallable()).unionPolygons();
        actual_polygons = &temp.getCallable();
        end_type = ClipperLib::etClosedPolygon;
    }
    else if constexpr (LineType::shape_type_ == ShapeType::Closed)
    {
        end_type = ClipperLib::etClosedLine;
    }
    else if (joinType == ClipperLib::jtMiter)
    {
        end_type = ClipperLib::etOpenSquare;
    }
    else
    {
        end_type = ClipperLib::etOpenRound;
    }
    ClipperLib::ClipperOffset clipper(miter_limit, 10.0);
    clipper.AddPaths(*actual_polygons, joinType, end_type);
    clipper.MiterLimit = miter_limit;
    clipper.Execute(ret.getCallable(), distance);
    return ret;
}
/*
Polygons Polygons::offset(const std::vector<coord_t>& offset_dists) const
{
    // we need as many offset-dists as points
    assert(pointCount() == offset_dists.size());

    Polygons ret;
    size_t i = 0;
    for (const path_t& poly_line : (*this)
                                       | ranges::views::filter(
                                           [](const path_t& path)
                                           {
                                               return ! path.empty();
                                           }))
    {
        Polygon ret_poly_line;

        auto prev_p = poly_line.back();
        auto prev_dist = offset_dists[i + poly_line.size() - 1];

        for (const point_t& p : poly_line)
        {
            auto offset_dist = offset_dists[i];

            auto vec_dir = prev_p - p;

            constexpr coord_t min_vec_len = 10;
            if (vSize2(vec_dir) > min_vec_len * min_vec_len)
            {
                auto offset_p1 = turn90CCW(normal(vec_dir, prev_dist));
                auto offset_p2 = turn90CCW(normal(vec_dir, offset_dist));

                ret_poly_line.emplace_back(prev_p + offset_p1);
                ret_poly_line.emplace_back(p + offset_p2);
            }

            prev_p = p;
            prev_dist = offset_dist;
            i++;
        }

        ret.push_back(ret_poly_line);
    }

    ClipperLib::SimplifyPolygons(ret.getCallable(), ClipperLib::PolyFillType::pftPositive);

    return ret;
}
*/
Polygons Polygons::getOutsidePolygons() const
{
    Polygons ret;
    ClipperLib::Clipper clipper(clipper_init);
    ClipperLib::PolyTree poly_tree;
    constexpr bool paths_are_closed_polys = true;
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, paths_are_closed_polys);
    clipper.Execute(ClipperLib::ctUnion, poly_tree);

    for (int outer_poly_idx = 0; outer_poly_idx < poly_tree.ChildCount(); outer_poly_idx++)
    {
        ClipperLib::PolyNode* child = poly_tree.Childs[outer_poly_idx];
        ret.emplace_back(child->Contour);
    }
    return ret;
}

Polygons Polygons::removeEmptyHoles() const
{
    Polygons ret;
    ClipperLib::Clipper clipper(clipper_init);
    ClipperLib::PolyTree poly_tree;
    constexpr bool paths_are_closed_polys = true;
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, paths_are_closed_polys);
    clipper.Execute(ClipperLib::ctUnion, poly_tree);

    bool remove_holes = true;
    removeEmptyHoles_processPolyTreeNode(poly_tree, remove_holes, ret);
    return ret;
}

Polygons Polygons::getEmptyHoles() const
{
    Polygons ret;
    ClipperLib::Clipper clipper(clipper_init);
    ClipperLib::PolyTree poly_tree;
    constexpr bool paths_are_closed_polys = true;
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, paths_are_closed_polys);
    clipper.Execute(ClipperLib::ctUnion, poly_tree);

    bool remove_holes = false;
    removeEmptyHoles_processPolyTreeNode(poly_tree, remove_holes, ret);
    return ret;
}

void Polygons::removeEmptyHoles_processPolyTreeNode(const ClipperLib::PolyNode& node, const bool remove_holes, Polygons& ret) const
{
    for (int outer_poly_idx = 0; outer_poly_idx < node.ChildCount(); outer_poly_idx++)
    {
        ClipperLib::PolyNode* child = node.Childs[outer_poly_idx];
        if (remove_holes)
        {
            ret.emplace_back(child->Contour);
        }
        for (int hole_node_idx = 0; hole_node_idx < child->ChildCount(); hole_node_idx++)
        {
            ClipperLib::PolyNode& hole_node = *child->Childs[hole_node_idx];
            if ((hole_node.ChildCount() > 0) == remove_holes)
            {
                ret.emplace_back(hole_node.Contour);
                removeEmptyHoles_processPolyTreeNode(hole_node, remove_holes, ret);
            }
        }
    }
}

void Polygons::removeSmallAreas(const double min_area_size, const bool remove_holes)
{
    auto new_end = end();
    if (remove_holes)
    {
        for (auto it = begin(); it < new_end;)
        {
            // All polygons smaller than target are removed by replacing them with a polygon from the back of the vector
            if (std::abs(INT2MM2(ClipperLib::Area(*it))) < min_area_size)
            {
                *it = std::move(*--new_end);
                continue;
            }
            it++; // Skipped on removal such that the polygon just swaped in is checked next
        }
    }
    else
    {
        // For each polygon, computes the signed area, move small outlines at the end of the vector and keep references on small holes
        std::vector<Polygon*> small_holes;
        for (auto it = begin(); it < new_end;)
        {
            double area = INT2MM2(ClipperLib::Area(*it));
            if (std::abs(area) < min_area_size)
            {
                if (area >= 0)
                {
                    --new_end;
                    if (it < new_end)
                    {
                        std::swap(*new_end, *it);
                        continue;
                    }
                    else
                    { // Don't self-swap the last Path
                        break;
                    }
                }
                else
                {
                    small_holes.push_back(&(*it));
                }
            }
            it++; // Skipped on removal such that the polygon just swaped in is checked next
        }

        // Removes small holes that have their first point inside one of the removed outlines
        // Iterating in reverse ensures that unprocessed small holes won't be moved
        const auto removed_outlines_start = new_end;
        for (auto hole_it = small_holes.rbegin(); hole_it < small_holes.rend(); hole_it++)
        {
            for (auto outline_it = removed_outlines_start; outline_it < end(); outline_it++)
            {
                if (outline_it->inside((*hole_it)->front()))
                {
                    **hole_it = std::move(*--new_end);
                    break;
                }
            }
        }
    }
    resize(new_end - begin());
}

void Polygons::removeSmallCircumference(const coord_t min_circumference_size, const bool remove_holes)
{
    removeSmallAreaCircumference(0.0, min_circumference_size, remove_holes);
}

void Polygons::removeSmallAreaCircumference(const double min_area_size, const coord_t min_circumference_size, const bool remove_holes)
{
    Polygons new_polygon;

    bool outline_is_removed = false;
    for (const Polygon& poly : (*this))
    {
        double area = poly.area();
        auto circumference = poly.length();
        bool is_outline = area >= 0;

        if (is_outline)
        {
            if (circumference >= min_circumference_size && std::abs(area) >= min_area_size)
            {
                new_polygon.push_back(poly);
                outline_is_removed = false;
            }
            else
            {
                outline_is_removed = true;
            }
        }
        else if (outline_is_removed)
        {
            // containing parent outline is removed; hole should be removed as well
        }
        else if (! remove_holes || (circumference >= min_circumference_size && std::abs(area) >= min_area_size))
        {
            // keep hole-polygon if we do not remove holes, or if its
            // circumference is bigger then the minimum circumference size
            new_polygon.push_back(poly);
        }
    }

    *this = new_polygon;
}

template<class LineType>
void LinesSet<LineType>::removeDegenerateVertsForEveryone()
{
    constexpr bool for_polyline = LineType::shape_type_ == ShapeType::Open;
    for (size_t poly_idx = 0; poly_idx < this->size(); poly_idx++)
    {
        Polygon& poly = (*this)[poly_idx];
        Polygon result;

        auto isDegenerate = [](const Point2LL& last, const Point2LL& now, const Point2LL& next)
        {
            Point2LL last_line = now - last;
            Point2LL next_line = next - now;
            return dot(last_line, next_line) == -1 * vSize(last_line) * vSize(next_line);
        };

        // With polylines, skip the first and last vertex.
        const size_t start_vertex = for_polyline ? 1 : 0;
        const size_t end_vertex = for_polyline ? poly.size() - 1 : poly.size();
        for (size_t i = 0; i < start_vertex; ++i)
        {
            result.push_back(poly[i]); // Add everything before the start vertex.
        }

        bool isChanged = false;
        for (size_t idx = start_vertex; idx < end_vertex; idx++)
        {
            const Point2LL& last = (result.size() == 0) ? poly.back() : result.back();
            if (idx + 1 >= poly.size() && result.size() == 0)
            {
                break;
            }
            const Point2LL& next = (idx + 1 >= poly.size()) ? result[0] : poly[idx + 1];
            if (isDegenerate(last, poly[idx], next))
            { // lines are in the opposite direction
                // don't add vert to the result
                isChanged = true;
                while (result.size() > 1 && isDegenerate(result[result.size() - 2], result.back(), next))
                {
                    result.pop_back();
                }
            }
            else
            {
                result.push_back(poly[idx]);
            }
        }

        for (size_t i = end_vertex; i < poly.size(); ++i)
        {
            result.push_back(poly[i]); // Add everything after the end vertex.
        }

        if (isChanged)
        {
            if (for_polyline || result.size() > 2)
            {
                poly = result;
            }
            else
            {
                remove(poly_idx);
                poly_idx--; // effectively the next iteration has the same poly_idx (referring to a new poly which is not yet processed)
            }
        }
    }
}

Polygons Polygons::remove(const Polygons& to_be_removed, int same_distance) const
{
    Polygons result;
    for (size_t poly_keep_idx = 0; poly_keep_idx < size(); poly_keep_idx++)
    {
        const Polygon& poly_keep = (*this)[poly_keep_idx];
        bool should_be_removed = false;
        if (poly_keep.size() > 0)
            //             for (int hole_poly_idx = 0; hole_poly_idx < to_be_removed.size(); hole_poly_idx++)
            for (const Polygon& poly_rem : to_be_removed)
            {
                //                 PolygonRef poly_rem = to_be_removed[hole_poly_idx];
                if (poly_rem.size() != poly_keep.size() || poly_rem.size() == 0)
                    continue;

                // find closest point, supposing this point aligns the two shapes in the best way
                size_t closest_point_idx = 0;
                coord_t smallestDist2 = -1;
                for (size_t point_rem_idx = 0; point_rem_idx < poly_rem.size(); point_rem_idx++)
                {
                    coord_t dist2 = vSize2(poly_rem[point_rem_idx] - poly_keep[0]);
                    if (dist2 < smallestDist2 || smallestDist2 < 0)
                    {
                        smallestDist2 = dist2;
                        closest_point_idx = point_rem_idx;
                    }
                }
                bool poly_rem_is_poly_keep = true;
                // compare the two polygons on all points
                if (smallestDist2 > same_distance * same_distance)
                    continue;
                for (size_t point_idx = 0; point_idx < poly_rem.size(); point_idx++)
                {
                    coord_t dist2 = vSize2(poly_rem[(closest_point_idx + point_idx) % poly_rem.size()] - poly_keep[point_idx]);
                    if (dist2 > same_distance * same_distance)
                    {
                        poly_rem_is_poly_keep = false;
                        break;
                    }
                }
                if (poly_rem_is_poly_keep)
                {
                    should_be_removed = true;
                    break;
                }
            }
        if (! should_be_removed)
            result.push_back(poly_keep);
    }
    return result;
}

Polygons Polygons::processEvenOdd(ClipperLib::PolyFillType poly_fill_type) const
{
    Polygons ret;
    ClipperLib::Clipper clipper(clipper_init);
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, true);
    clipper.Execute(ClipperLib::ctUnion, ret.getCallable(), poly_fill_type);
    return ret;
}

Polygons Polygons::toPolygons(ClipperLib::PolyTree& poly_tree)
{
    Polygons ret;
    ClipperLib::PolyTreeToPaths(poly_tree, ret.getCallable());
    return ret;
}

[[maybe_unused]] Polygons Polygons::fromWkt(const std::string& wkt)
{
    typedef boost::geometry::model::d2::point_xy<double> point_type;
    typedef boost::geometry::model::polygon<point_type> polygon_type;

    polygon_type poly;
    boost::geometry::read_wkt(wkt, poly);

    Polygons ret;

    Polygon outer;
    for (const auto& point : poly.outer())
    {
        outer.push_back(point_t(point.x(), point.y()));
    }
    ret.push_back(outer);

    for (const auto& hole : poly.inners())
    {
        Polygon inner;
        for (const auto& point : hole)
        {
            inner.push_back(point_t(point.x(), point.y()));
        }
        ret.push_back(inner);
    }

    return ret;
}

[[maybe_unused]] void Polygons::writeWkt(std::ostream& stream) const
{
    stream << "POLYGON (";
    const auto paths_str = (*this)
                         | ranges::views::transform(
                               [](const auto& path)
                               {
                                   const auto line_string = ranges::views::concat(path, path | ranges::views::take(1))
                                                          | ranges::views::transform(
                                                                [](const auto& point)
                                                                {
                                                                    return fmt::format("{} {}", point.X, point.Y);
                                                                })
                                                          | ranges::views::join(ranges::views::c_str(", ")) | ranges::to<std::string>();
                                   return "(" + line_string + ")";
                               })
                         | ranges::views::join(ranges::views::c_str(", ")) | ranges::to<std::string>();
    stream << paths_str;
    stream << ")";
}

Polygons Polygons::smooth_outward(const AngleDegrees max_angle, int shortcut_length) const
{
    Polygons ret;
    for (const Polygon& poly : (*this))
    {
        if (poly.size() < 3)
        {
            continue;
        }
        if (poly.size() == 3)
        {
            ret.push_back(poly);
            continue;
        }
        poly.smooth_outward(max_angle, shortcut_length, ret.newPoly());
        if (ret.back().size() < 3)
        {
            ret.resize(ret.size() - 1);
        }
    }
    return ret;
}

Polygons Polygons::smooth(int remove_length) const
{
    Polygons ret;
    for (const Polygon& poly : (*this))
    {
        if (poly.size() < 3)
        {
            continue;
        }
        if (poly.size() == 3)
        {
            ret.push_back(poly);
            continue;
        }
        poly.smooth(remove_length, ret.newPoly());
        Polygon& back = ret.back();
        if (back.size() < 3)
        {
            back.resize(back.size() - 1);
        }
    }
    return ret;
}

Polygons Polygons::smooth2(int remove_length, int min_area) const
{
    Polygons ret;
    for (const Polygon& poly : (*this))
    {
        if (poly.size() == 0)
        {
            continue;
        }
        if (poly.area() < min_area || poly.size() <= 5) // when optimally removing, a poly with 5 pieces results in a triangle. Smaller polys dont have area!
        {
            ret.push_back(poly);
            continue;
        }
        if (poly.size() < 4)
        {
            ret.push_back(poly);
        }
        else
        {
            poly.smooth2(remove_length, ret.newPoly());
        }
    }
    return ret;
}

void Polygons::removeColinearEdges(const AngleRadians max_deviation_angle)
{
    Polygons& thiss = *this;
    for (size_t p = 0; p < size(); p++)
    {
        thiss[p].removeColinearEdges(max_deviation_angle);
        if (thiss[p].size() < 3)
        {
            remove(p);
            p--;
        }
    }
}

void Polygons::scale(const Ratio& ratio)
{
    if (ratio == 1.)
    {
        return;
    }

    for (auto& points : *this)
    {
        for (auto& pt : points)
        {
            pt = pt * static_cast<double>(ratio);
        }
    }
}

void Polygons::translate(const point_t& delta)
{
    if (delta.X != 0 || delta.Y != 0)
    {
        for (Polygon& polygon : *this)
        {
            polygon.translate(delta);
        }
    }
}

double Polygons::area() const
{
    return std::accumulate(
        begin(),
        end(),
        0.0,
        [](double total, const Polygon& poly)
        {
            return total + poly.area();
        });

    double area = 0.0;
    for (unsigned int poly_idx = 0; poly_idx < size(); poly_idx++)
    {
        area += operator[](poly_idx).area();
        // note: holes already have negative area
    }
    return area;
}

std::vector<PolygonsPart> Polygons::splitIntoParts(bool unionAll) const
{
    std::vector<PolygonsPart> ret;
    ClipperLib::Clipper clipper(clipper_init);
    ClipperLib::PolyTree resultPolyTree;
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, true);
    if (unionAll)
        clipper.Execute(ClipperLib::ctUnion, resultPolyTree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    else
        clipper.Execute(ClipperLib::ctUnion, resultPolyTree);

    splitIntoParts_processPolyTreeNode(&resultPolyTree, ret);
    return ret;
}

void Polygons::splitIntoParts_processPolyTreeNode(ClipperLib::PolyNode* node, std::vector<PolygonsPart>& ret) const
{
    for (int n = 0; n < node->ChildCount(); n++)
    {
        ClipperLib::PolyNode* child = node->Childs[n];
        PolygonsPart part;
        part.emplace_back(child->Contour);
        for (int i = 0; i < child->ChildCount(); i++)
        {
            part.emplace_back(child->Childs[i]->Contour);
            splitIntoParts_processPolyTreeNode(child->Childs[i], ret);
        }
        ret.push_back(part);
    }
}

std::vector<Polygons> Polygons::sortByNesting() const
{
    std::vector<Polygons> ret;
    ClipperLib::Clipper clipper(clipper_init);
    ClipperLib::PolyTree resultPolyTree;
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, true);
    clipper.Execute(ClipperLib::ctUnion, resultPolyTree);

    sortByNesting_processPolyTreeNode(&resultPolyTree, 0, ret);
    return ret;
}

void Polygons::sortByNesting_processPolyTreeNode(ClipperLib::PolyNode* node, const size_t nesting_idx, std::vector<Polygons>& ret) const
{
    for (int n = 0; n < node->ChildCount(); n++)
    {
        ClipperLib::PolyNode* child = node->Childs[n];
        if (nesting_idx >= ret.size())
        {
            ret.resize(nesting_idx + 1);
        }
        ret[nesting_idx].emplace_back(child->Contour);
        sortByNesting_processPolyTreeNode(child, nesting_idx + 1, ret);
    }
}

Polygons Polygons::tubeShape(const coord_t inner_offset, const coord_t outer_offset) const
{
    return this->offset(outer_offset).difference(this->offset(-inner_offset));
}

size_t PartsView::getPartContaining(size_t poly_idx, size_t* boundary_poly_idx) const
{
    const PartsView& partsView = *this;
    for (size_t part_idx_now = 0; part_idx_now < partsView.size(); part_idx_now++)
    {
        const std::vector<size_t>& partView = partsView[part_idx_now];
        if (partView.size() == 0)
        {
            continue;
        }
        std::vector<size_t>::const_iterator result = std::find(partView.begin(), partView.end(), poly_idx);
        if (result != partView.end())
        {
            if (boundary_poly_idx)
            {
                *boundary_poly_idx = partView[0];
            }
            return part_idx_now;
        }
    }
    return NO_INDEX;
}

PolygonsPart PartsView::assemblePart(size_t part_idx) const
{
    const PartsView& partsView = *this;
    PolygonsPart ret;
    if (part_idx != NO_INDEX)
    {
        for (size_t poly_idx_ff : partsView[part_idx])
        {
            ret.push_back(polygons_[poly_idx_ff]);
        }
    }
    return ret;
}

PolygonsPart PartsView::assemblePartContaining(size_t poly_idx, size_t* boundary_poly_idx) const
{
    PolygonsPart ret;
    size_t part_idx = getPartContaining(poly_idx, boundary_poly_idx);
    if (part_idx != NO_INDEX)
    {
        return assemblePart(part_idx);
    }
    return ret;
}

PartsView Polygons::splitIntoPartsView(bool unionAll)
{
    Polygons reordered;
    PartsView partsView(*this);
    ClipperLib::Clipper clipper(clipper_init);
    ClipperLib::PolyTree resultPolyTree;
    clipper.AddPaths(getCallable(), ClipperLib::ptSubject, true);
    if (unionAll)
        clipper.Execute(ClipperLib::ctUnion, resultPolyTree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    else
        clipper.Execute(ClipperLib::ctUnion, resultPolyTree);

    splitIntoPartsView_processPolyTreeNode(partsView, reordered, &resultPolyTree);

    (*this) = reordered;
    return partsView;
}

void Polygons::splitIntoPartsView_processPolyTreeNode(PartsView& partsView, Polygons& reordered, ClipperLib::PolyNode* node) const
{
    for (int n = 0; n < node->ChildCount(); n++)
    {
        ClipperLib::PolyNode* child = node->Childs[n];
        partsView.emplace_back();
        size_t pos = partsView.size() - 1;
        partsView[pos].push_back(reordered.size());
        reordered.emplace_back(child->Contour); // TODO: should this steal the internal representation for speed?
        for (int i = 0; i < child->ChildCount(); i++)
        {
            partsView[pos].push_back(reordered.size());
            reordered.emplace_back(child->Childs[i]->Contour);
            splitIntoPartsView_processPolyTreeNode(partsView, reordered, child->Childs[i]);
        }
    }
}

void Polygons::ensureManifold()
{
    std::vector<Point2LL> duplicate_locations;
    std::unordered_set<Point2LL> poly_locations;
    for (const Polygon& poly : (*this))
    {
        for (const point_t& p : poly)
        {
            if (poly_locations.find(p) != poly_locations.end())
            {
                duplicate_locations.push_back(p);
            }
            poly_locations.emplace(p);
        }
    }
    Polygons removal_dots;
    for (Point2LL p : duplicate_locations)
    {
        Polygon& dot = removal_dots.newPoly();
        dot.push_back(p + Point2LL(0, 5));
        dot.push_back(p + Point2LL(5, 0));
        dot.push_back(p + Point2LL(0, -5));
        dot.push_back(p + Point2LL(-5, 0));
    }
    if (! removal_dots.empty())
    {
        *this = difference(removal_dots);
    }
}

coord_t Polygons::length() const
{
    return std::accumulate(
        begin(),
        end(),
        0,
        [](coord_t total, const Polygon& polygon)
        {
            return total += polygon.length();
        });
}

point_t Polygons::min() const
{
    point_t ret = point_t(POINT_MAX, POINT_MAX);

    for (const Polygon& polygon : *this)
    {
        for (const point_t& p : polygon)
        {
            ret.X = std::min(ret.X, p.X);
            ret.Y = std::min(ret.Y, p.Y);
        }
    }

    return ret;
}

point_t Polygons::max() const
{
    point_t ret = point_t(POINT_MIN, POINT_MIN);

    for (const Polygon& polygon : *this)
    {
        for (const point_t& p : polygon)
        {
            ret.X = std::max(ret.X, p.X);
            ret.Y = std::max(ret.Y, p.Y);
        }
    }

    return ret;
}

void Polygons::applyMatrix(const PointMatrix& matrix)
{
    for (Polygon& polygon : *this)
    {
        polygon.applyMatrix(matrix);
    }
}

void Polygons::applyMatrix(const Point3Matrix& matrix)
{
    for (Polygon& polygon : *this)
    {
        polygon.applyMatrix(matrix);
    }
}

} // namespace cura
