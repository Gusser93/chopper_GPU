#pragma once

#include <seqan/seq_io.h>

#include <chopper/split/minimizer.hpp>
#include <chopper/split/split_config.hpp>
#include <chopper/split/split_data.hpp>

inline bool load_minimizer_sequences(split_data & data,
                                     batch_config const & config,
                                     const char *fileName)
{
    seqan::SeqFileIn inFile;
    if (!open(inFile, fileName))
    {
        std::cerr << "Could not open " << fileName << " for reading!" << std::endl;
        return false;
    }

    if (config.verbose)
        std::cerr << ">>> Processing file " << fileName << std::endl;

    seqan::StringSet<seqan::String<seqan::Dna>, seqan::Owner<>> sequences;
    {
        seqan::StringSet<seqan::String<seqan::Iupac>, seqan::Owner<>> iupac_sequences;
        seqan::readRecords(data.ids, iupac_sequences, inFile);

        seqan::resize(sequences, seqan::length(iupac_sequences));
        data.files_of_origin.reserve(seqan::length(iupac_sequences));

        for (size_t idx = 0; idx < seqan::length(iupac_sequences); ++idx)
        {
            for (size_t jdx = 0; jdx < seqan::length(iupac_sequences[idx]); ++jdx)
            {
                seqan::appendValue(sequences[idx], iupac_sequences[idx][jdx]);
            }
            seqan::appendValue(data.lengths, seqan::length(iupac_sequences[idx]));
            data.files_of_origin.push_back(fileName);
        }
    }

    // compute minimizers per sequence and store the corresponding chain in data.sequences
    auto from = seqan::length(data.sequences);
    resize(data.sequences, seqan::length(data.sequences) + seqan::length(sequences));
    Minimizer mini;
    mini.resize(config.kmer_size, config.window_size);
    for (size_t idx = from; idx < seqan::length(data.sequences); ++idx)
    {
        data.sequences[idx] = mini.getMinimizer(sequences[idx - from]);
        // seqan3::debug_stream << seqan::length(data.sequences[idx]) << std::endl;
    }

    return (seqan::length(data.ids) > 0u);
}
