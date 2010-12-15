// Boost.Geometry (aka GGL, Generic Geometry Library)
//
// Copyright Barend Gehrels 2007-2009, Geodan, Amsterdam, the Netherlands.
// Copyright Bruno Lalande 2008, 2009
// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_GEOMETRY_ALGORITHMS_DETAIL_SECTIONS_SECTIONALIZE_HPP
#define BOOST_GEOMETRY_ALGORITHMS_DETAIL_SECTIONS_SECTIONALIZE_HPP

#include <cstddef>
#include <vector>

#include <boost/mpl/assert.hpp>
#include <boost/range.hpp>
#include <boost/typeof/typeof.hpp>

#include <boost/geometry/algorithms/assign.hpp>
#include <boost/geometry/algorithms/combine.hpp>

#include <boost/geometry/core/access.hpp>
#include <boost/geometry/core/closure.hpp>
#include <boost/geometry/core/exterior_ring.hpp>
#include <boost/geometry/core/point_order.hpp>

#include <boost/geometry/geometries/concepts/check.hpp>
#include <boost/geometry/util/math.hpp>
#include <boost/geometry/util/closeable_view.hpp>
#include <boost/geometry/geometries/segment.hpp>


namespace boost { namespace geometry
{


/*!
    \brief Structure containing section information
    \details Section information consists of a bounding box, direction
        information (if it is increasing or decreasing, per dimension),
        index information (begin-end, ring, multi) and the number of
        segments in this section

    \tparam Box box-type
    \tparam DimensionCount number of dimensions for this section
    \ingroup sectionalize
 */
template <typename Box, std::size_t DimensionCount>
struct section
{
    typedef Box box_type;

    // unique ID used in get_turns to mark section-pairs already handled.
    int id;

    int directions[DimensionCount];
    int ring_index;
    int multi_index;
    Box bounding_box;

    int begin_index;
    int end_index;
    std::size_t count;
    std::size_t range_count;
    bool duplicate;
    int non_duplicate_index;

    inline section()
        : id(-1)
        , ring_index(-99)
        , multi_index(-99)
        , begin_index(-1)
        , end_index(-1)
        , count(0)
        , range_count(0)
        , duplicate(false)
        , non_duplicate_index(-1)
    {
        assign_inverse(bounding_box);
        for (register std::size_t i = 0; i < DimensionCount; i++)
        {
            directions[i] = 0;
        }
    }
};


/*!
    \brief Structure containing a collection of sections
    \note Derived from a vector, proves to be faster than of deque
    \note vector might be templated in the future
    \ingroup sectionalize
 */
template <typename Box, std::size_t DimensionCount>
struct sections : std::vector<section<Box, DimensionCount> >
{
    typedef Box box_type;
    static std::size_t const value = DimensionCount;
};


#ifndef DOXYGEN_NO_DETAIL
namespace detail { namespace sectionalize
{

template <typename Segment, std::size_t Dimension, std::size_t DimensionCount>
struct get_direction_loop
{
    typedef typename coordinate_type<Segment>::type coordinate_type;

    static inline void apply(Segment const& seg,
                int directions[DimensionCount])
    {
        coordinate_type const diff =
            geometry::get<1, Dimension>(seg) - geometry::get<0, Dimension>(seg);

        coordinate_type zero = coordinate_type();
        directions[Dimension] = diff > zero ? 1 : diff < zero ? -1 : 0;

        get_direction_loop
            <
                Segment, Dimension + 1, DimensionCount
            >::apply(seg, directions);
    }
};

template <typename Segment, std::size_t DimensionCount>
struct get_direction_loop<Segment, DimensionCount, DimensionCount>
{
    static inline void apply(Segment const&, int [DimensionCount])
    {}
};

template <typename T, std::size_t Dimension, std::size_t DimensionCount>
struct copy_loop
{
    static inline void apply(T const source[DimensionCount],
                T target[DimensionCount])
    {
        target[Dimension] = source[Dimension];
        copy_loop<T, Dimension + 1, DimensionCount>::apply(source, target);
    }
};

template <typename T, std::size_t DimensionCount>
struct copy_loop<T, DimensionCount, DimensionCount>
{
    static inline void apply(T const [DimensionCount], T [DimensionCount])
    {}
};

template <typename T, std::size_t Dimension, std::size_t DimensionCount>
struct compare_loop
{
    static inline bool apply(T const source[DimensionCount],
                T const target[DimensionCount])
    {
        bool const not_equal = target[Dimension] != source[Dimension];

        return not_equal
            ? false
            : compare_loop
                <
                    T, Dimension + 1, DimensionCount
                >::apply(source, target);
    }
};

template <typename T, std::size_t DimensionCount>
struct compare_loop<T, DimensionCount, DimensionCount>
{
    static inline bool apply(T const [DimensionCount],
                T const [DimensionCount])
    {

        return true;
    }
};


template <typename Segment, std::size_t Dimension, std::size_t DimensionCount>
struct check_duplicate_loop
{
    typedef typename coordinate_type<Segment>::type coordinate_type;

    static inline bool apply(Segment const& seg)
    {
        coordinate_type const diff =
            geometry::get<1, Dimension>(seg) - geometry::get<0, Dimension>(seg);

        coordinate_type const zero = 0;
        if (! geometry::math::equals(diff, zero))
        {
            return false;
        }

        return check_duplicate_loop
            <
                Segment, Dimension + 1, DimensionCount
            >::apply(seg);
    }
};

template <typename Segment, std::size_t DimensionCount>
struct check_duplicate_loop<Segment, DimensionCount, DimensionCount>
{
    static inline bool apply(Segment const&)
    {
        return true;
    }
};

template <typename T, std::size_t Dimension, std::size_t DimensionCount>
struct assign_loop
{
    static inline void apply(T dims[DimensionCount], int const value)
    {
        dims[Dimension] = value;
        assign_loop<T, Dimension + 1, DimensionCount>::apply(dims, value);
    }
};

template <typename T, std::size_t DimensionCount>
struct assign_loop<T, DimensionCount, DimensionCount>
{
    static inline void apply(T [DimensionCount], int const)
    {
    }
};

/// @brief Helper class to create sections of a part of a range, on the fly
template
<
    typename Range,  // Can be closeable_view
    typename Point,
    typename Sections,
    std::size_t DimensionCount,
    std::size_t MaxCount
>
struct sectionalize_part
{
    typedef model::referring_segment<Point const> segment_type;
    typedef typename boost::range_value<Sections>::type section_type;

    typedef typename boost::range_iterator<Range const>::type iterator_type;

    static inline void apply(Sections& sections, section_type& section,
                int& index, int& ndi,
                Range const& range,
                int ring_index = -1, int multi_index = -1)
    {
        if (boost::size(range) <= index)
        {
            return;
        }

        if (index == 0)
        {
            ndi = 0;
        }

        iterator_type it = boost::begin(range);
        it += index;

        for(iterator_type previous = it++;
            it != boost::end(range);
            ++previous, ++it, index++)
        {
            segment_type segment(*previous, *it);

            int direction_classes[DimensionCount] = {0};
            get_direction_loop
                <
                    segment_type, 0, DimensionCount
                >::apply(segment, direction_classes);

            // if "dir" == 0 for all point-dimensions, it is duplicate.
            // Those sections might be omitted, if wished, lateron
            bool duplicate = false;

            if (direction_classes[0] == 0)
            {
                // Recheck because ALL dimensions should be checked,
                // not only first one.
                // (DimensionCount might be < dimension<P>::value)
                if (check_duplicate_loop
                    <
                        segment_type, 0, geometry::dimension<Point>::type::value
                    >::apply(segment)
                    )
                {
                    duplicate = true;

                    // Change direction-info to force new section
                    // Note that wo consecutive duplicate segments will generate
                    // only one duplicate-section.
                    // Actual value is not important as long as it is not -1,0,1
                    assign_loop
                    <
                        int, 0, DimensionCount
                    >::apply(direction_classes, -99);
                }
            }

            if (section.count > 0
                && (!compare_loop
                        <
                            int, 0, DimensionCount
                        >::apply(direction_classes, section.directions)
                    || section.count > MaxCount
                    )
                )
            {
                sections.push_back(section);
                section = section_type();
            }

            if (section.count == 0)
            {
                section.begin_index = index;
                section.ring_index = ring_index;
                section.multi_index = multi_index;
                section.duplicate = duplicate;
                section.non_duplicate_index = ndi;
                section.range_count = boost::size(range);

                copy_loop
                    <
                        int, 0, DimensionCount
                    >::apply(direction_classes, section.directions);
                geometry::combine(section.bounding_box, *previous);
            }

            geometry::combine(section.bounding_box, *it);
            section.end_index = index + 1;
            section.count++;
            if (! duplicate)
            {
                ndi++;
            }
        }
    }
};


template
<
    typename Range, closure_selector Closure,
    typename Point,
    typename Sections,
    std::size_t DimensionCount,
    std::size_t MaxCount
>
struct sectionalize_range
{
    typedef typename closeable_view<Range const, Closure>::type view_type;

    static inline void apply(Range const& range, Sections& sections,
                int ring_index = -1, int multi_index = -1)
    {
        typedef model::referring_segment<Point const> segment_type;

        view_type view(range);

        std::size_t const n = boost::size(view);
        if (n == 0)
        {
            // Zero points, no section
            return;
        }

        if (n == 1)
        {
            // Line with one point ==> no sections
            return;
        }

        int index = 0;
        int ndi = 0; // non duplicate index

        typedef typename boost::range_value<Sections>::type section_type;
        section_type section;

        sectionalize_part
            <
                view_type, Point, Sections,
                DimensionCount, MaxCount
            >::apply(sections, section, index, ndi,
                        view, ring_index, multi_index);

        // Add last section if applicable
        if (section.count > 0)
        {
            sections.push_back(section);
        }
    }
};

template
<
    typename Polygon,
    typename Sections,
    std::size_t DimensionCount,
    std::size_t MaxCount
>
struct sectionalize_polygon
{
    static inline void apply(Polygon const& poly, Sections& sections,
                int multi_index = -1)
    {
        typedef typename point_type<Polygon>::type point_type;
        typedef typename ring_type<Polygon>::type ring_type;
        typedef sectionalize_range
            <
                ring_type, closure<Polygon>::value,
                point_type, Sections, DimensionCount, MaxCount
            > sectionalizer_type;

        sectionalizer_type::apply(exterior_ring(poly), sections, -1, multi_index);

        int i = 0;

        typename interior_return_type<Polygon const>::type rings
                    = interior_rings(poly);
        for (BOOST_AUTO(it, boost::begin(rings)); it != boost::end(rings);
             ++it, ++i)
        {
            sectionalizer_type::apply(*it, sections, i, multi_index);
        }
    }
};

template
<
    typename Box,
    typename Sections,
    std::size_t DimensionCount,
    std::size_t MaxCount
>
struct sectionalize_box
{
    static inline void apply(Box const& box, Sections& sections)
    {
        typedef typename point_type<Box>::type point_type;

        assert_dimension<Box, 2>();

        // Add all four sides of the 2D-box as separate section.
        // Easiest is to convert it to a polygon.
        // However, we don't have the polygon type
        // (or polygon would be a helper-type).
        // Therefore we mimic a linestring/std::vector of 5 points

        point_type ll, lr, ul, ur;
        assign_box_corners(box, ll, lr, ul, ur);

        std::vector<point_type> points;
        points.push_back(ll);
        points.push_back(ul);
        points.push_back(ur);
        points.push_back(lr);
        points.push_back(ll);

        sectionalize_range
            <
                std::vector<point_type>, closed,
                point_type,
                Sections,
                DimensionCount,
                MaxCount
            >::apply(points, sections);
    }
};

template <typename Sections>
inline void set_section_unique_ids(Sections& sections)
{
    // Set ID's.
    int index = 0;
    for (typename boost::range_iterator<Sections>::type it = boost::begin(sections);
        it != boost::end(sections);
        ++it)
    {
        it->id = index++;
    }
}


}} // namespace detail::sectionalize
#endif // DOXYGEN_NO_DETAIL


#ifndef DOXYGEN_NO_DISPATCH
namespace dispatch
{

template
<
    typename Tag,
    typename Geometry,
    typename Sections,
    std::size_t DimensionCount,
    std::size_t MaxCount
>
struct sectionalize
{
    BOOST_MPL_ASSERT_MSG
        (
            false, NOT_OR_NOT_YET_IMPLEMENTED_FOR_THIS_GEOMETRY_TYPE
            , (types<Geometry>)
        );
};

template
<
    typename Box,
    typename Sections,
    std::size_t DimensionCount,
    std::size_t MaxCount
>
struct sectionalize<box_tag, Box, Sections, DimensionCount, MaxCount>
    : detail::sectionalize::sectionalize_box
        <
            Box,
            Sections,
            DimensionCount,
            MaxCount
        >
{};

template
<
    typename LineString, typename
    Sections,
    std::size_t DimensionCount,
    std::size_t MaxCount
>
struct sectionalize
    <
        linestring_tag,
        LineString,
        Sections,
        DimensionCount,
        MaxCount
    >
    : detail::sectionalize::sectionalize_range
        <
            LineString, closed,
            typename point_type<LineString>::type,
            Sections,
            DimensionCount,
            MaxCount
        >
{};

template
<
    typename Ring,
    typename Sections,
    std::size_t DimensionCount,
    std::size_t MaxCount
>
struct sectionalize<ring_tag, Ring, Sections, DimensionCount, MaxCount>
    : detail::sectionalize::sectionalize_range
        <
            Ring, geometry::closure<Ring>::value,
            typename point_type<Ring>::type,
            Sections,
            DimensionCount,
            MaxCount
        >
{};

template
<
    typename Polygon,
    typename Sections,
    std::size_t DimensionCount,
    std::size_t MaxCount
>
struct sectionalize<polygon_tag, Polygon, Sections, DimensionCount, MaxCount>
    : detail::sectionalize::sectionalize_polygon
        <
            Polygon, Sections, DimensionCount, MaxCount
        >
{};

} // namespace dispatch
#endif


/*!
    \brief Split a geometry into monotonic sections
    \ingroup sectionalize
    \tparam Geometry type of geometry to check
    \tparam Sections type of sections to create
    \param geometry geometry to create sections from
    \param sections structure with sections

 */
template<typename Geometry, typename Sections>
inline void sectionalize(Geometry const& geometry, Sections& sections)
{
    concept::check<Geometry const>();

    // A maximum of 10 segments per section seems to give the fastest results
    static std::size_t const max_segments_per_section = 10;
    typedef dispatch::sectionalize
        <
            typename tag<Geometry>::type,
            Geometry,
            Sections,
            Sections::value,
            max_segments_per_section
        > sectionalizer_type;

    sections.clear();
    sectionalizer_type::apply(geometry, sections);
    detail::sectionalize::set_section_unique_ids(sections);
}


}} // namespace boost::geometry


#endif // BOOST_GEOMETRY_ALGORITHMS_DETAIL_SECTIONS_SECTIONALIZE_HPP
