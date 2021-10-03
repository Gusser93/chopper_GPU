#pragma once

#include <lemon/color.h>
#include <lemon/concepts/digraph.h>
#include <lemon/core.h>
#include <lemon/dimacs.h>
#include <lemon/list_graph.h>
#include <lemon/static_graph.h>

#include <seqan/graph_msa.h>

#include <seqan3/core/debug_stream.hpp>

#include <chopper/split/split_config.hpp>
#include <chopper/split/split_data.hpp>

/* [source] --> [target]            [target]
 *  (2,10)       (10,15)     =>      (2,15)
 *
 * [source] --> [target]            [target]
 *   (0,0)       (10,15)     =>      (10,15)
 *
 * [source] --> [target]            [target]
 *  (2,10)       (0,0)       =>      (2,10)
 */
template <typename node_map_type>
void merge_properties_into(node_map_type            & node_map,
                           typename node_map_type::Key const & source,
                           typename node_map_type::Key const & target)
{
    typename node_map_type::Value & target_value = node_map[target];

    uint16_t idx{};
    for (auto const & [range_start, range_end] : node_map[source])
    {
        assert(range_end >= range_start);
        if (range_start != 0 || range_end != 0) // source non empty
        {
            if (target_value[idx].first == 0 && target_value[idx].second == 0) // target empty
            {
                target_value[idx].second = range_end;
            }
            else
            {
                if (range_end != target_value[idx].first)
                    throw std::runtime_error{"Possibly corrupted input graph (Range idx " + std::to_string(idx) +
                                             "). Source range: [" + std::to_string(range_start) +
                                             ", " + std::to_string(range_end) +
                                             "]Error: source range end was not equal the target range start: " +
                                             std::to_string(target_value[idx].first)};
            }

            target_value[idx].first = range_start;
        }
        ++idx;
    }
}

template <typename graph_type>
void transfer_nodes(lemon::ListDigraph & lemon_graph,
                    std::vector<lemon::ListDigraph::Node> & nodes,
                    lemon::ListDigraph::NodeMap<std::vector<std::pair<uint32_t, uint32_t>>> & node_map,
                    graph_type const & seqan2_graph,
                    split_data const & data)
{
    typedef typename seqan::Id<graph_type>::Type TIdType;
    typedef typename seqan::Iterator<graph_type, seqan::VertexIterator>::Type TConstIter;

    // temporary storage
    size_t const number_of_sequences = length(data.sequences);
    size_t const number_of_nodes = seqan::numVertices(seqan2_graph);

    nodes.reserve(number_of_nodes + 1);
    lemon_graph.reserveNode(number_of_nodes + 1);
    lemon_graph.reserveArc(number_of_nodes + 1);

    lemon::ListDigraph::Node source_node = lemon_graph.addNode();
    lemon::ListDigraph::Node sink_node = lemon_graph.addNode();
    nodes.push_back(source_node);
    nodes.push_back(sink_node);
    node_map.set(source_node, std::vector<std::pair<uint32_t, uint32_t>>(number_of_sequences));
    node_map.set(sink_node, std::vector<std::pair<uint32_t, uint32_t>>(number_of_sequences));

    for (TConstIter it(seqan2_graph); !seqan::atEnd(it); ++it) // iterate over seqan2 nodes
    {
        TIdType id = seqan::sequenceId(seqan2_graph, *it);

        // fragment start and end
        auto regStart = seqan::fragmentBegin(seqan2_graph, *it);
        auto regEnd = seqan::fragmentBegin(seqan2_graph, *it) + seqan::fragmentLength(seqan2_graph, *it);

        uint32_t range_start{};
        uint32_t range_end{};

        if (regStart == 0)
            range_start = 0; // if it is the very first minimizer, include beginning of the sequence
        else
            range_start = data.sequences[id][regStart].position;

        if (regEnd >= seqan::length(data.sequences[id]))
            range_end = data.lengths[id];
        else
            range_end = data.sequences[id][regEnd].position;

        assert(range_end >= range_start);

        auto node = lemon_graph.addNode();
        nodes.push_back(node);

        // add arc from source node to node if it is a start node (start of sequence range)
        if (range_start == 0) // [unlikely]
            lemon_graph.addArc(source_node, node);

        if (range_end == data.lengths[id]) // [unlikely]
        {
            // seqan3::debug_stream << "i:" << i << " group[i]:" << group[i] << " data.lengths[group[i]]:" << data.lengths[group[i]] << std::endl;
            lemon_graph.addArc(node, sink_node);
        }

        // fill node map of 'node'
        std::vector<std::pair<uint32_t, uint32_t>> node_property(number_of_sequences);
        assert(id < number_of_sequences);
        node_property[id] = std::make_pair(range_start, range_end);
        node_map.set(node, node_property);
    }
}

template <typename graph_type>
void transfer_directed_edges(lemon::ListDigraph & lemon_graph,
                             std::vector<lemon::ListDigraph::Node> & nodes,
                             graph_type const & seqan2_graph)
{
    typedef typename seqan::Size<graph_type>::Type TSize;
    typedef typename seqan::Id<graph_type>::Type TId;
    typedef typename seqan::VertexDescriptor<graph_type>::Type TVertexDescriptor;

    auto const & str = seqan::stringSet(seqan2_graph);
    TSize len = seqan::length(str);
    TVertexDescriptor nilVertex = seqan::getNil<TVertexDescriptor>();

    for (TSize i = 0; i < len; ++i)
    {
        TId seqId = seqan::positionToId(str, i);
        TSize j = 0;
        TVertexDescriptor previousVertex = nilVertex;

        while (j < seqan::length(str[i]))
        {
            TVertexDescriptor nextVertex = seqan::findVertex(const_cast<graph_type&>(seqan2_graph), seqId, j);

            if (nextVertex == nilVertex)
            {
                ++j;
                continue;
            }

            // plus 2 because ids were shifted when we inserted a source/sink node
            if (previousVertex != nilVertex)
                lemon_graph.addArc(nodes[previousVertex + 2], nodes[nextVertex + 2]);

            previousVertex = nextVertex;
            j += seqan::fragmentLength(seqan2_graph, nextVertex);
        }
    }
}

template <typename graph_type>
void transfer_undirected_edges(lemon::ListDigraph & lemon_graph,
                               std::vector<lemon::ListDigraph::Node> & nodes,
                               lemon::ListDigraph::NodeMap<std::vector<std::pair<uint32_t, uint32_t>>> & node_map,
                               graph_type const & seqan2_graph)
{
    typedef typename seqan::VertexDescriptor<graph_type>::Type TVertexDescriptor;
    typedef typename seqan::Iterator<graph_type, seqan::EdgeIterator>::Type TConstEdIter;

    for (TConstEdIter itEd(seqan2_graph); !seqan::atEnd(itEd); ++itEd)
    {
        TVertexDescriptor source = seqan::sourceVertex(itEd);
        TVertexDescriptor target = seqan::targetVertex(itEd);

        // plus 2 because ids were shifted when we inserted a source/sink node
        source += 2;
        target += 2;

        // the undirected edges connect minimizer
        if (nodes[source] != nodes[target]) // if not combined already
        {
            merge_properties_into(node_map, nodes[target], nodes[source]);

            // merge target into source (true to remove any loops)
            lemon_graph.contract(nodes[source], nodes[target], true);
            nodes[target] = nodes[source]; // merge target into source
        }
    }
}
// currently through an output file
template <typename graph_type>
void transform_graphs(lemon::ListDigraph & g,
                      std::vector<lemon::ListDigraph::Node> & nodes,
                      lemon::ListDigraph::NodeMap<std::vector<std::pair<uint32_t, uint32_t>>> & node_map,
                      graph_type const & seqan2_graph,
                      split_data const & data,
                      batch_config const & config)
{
    transfer_nodes(g, nodes, node_map, seqan2_graph, data);

    if (config.verbose)
        seqan3::debug_stream << "[LOG] inserted " <<  lemon::countNodes(g) << " nodes into the graph." << std::endl;

    transfer_directed_edges(g, nodes, seqan2_graph);

    if (config.verbose)
        seqan3::debug_stream << "[LOG] inserted " <<  lemon::countArcs(g) << " arcs into the graph." << std::endl;

    transfer_undirected_edges(g, nodes, node_map, seqan2_graph);
}
