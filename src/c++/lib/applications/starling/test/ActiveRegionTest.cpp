// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Strelka - Small Variant Caller
// Copyright (c) 2009-2016 Illumina, Inc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//

///
/// \author Sangtae Kim
///

#include <applications/starling/starling_shared.hh>
#include <starling_common/IndelBuffer.hh>
#include <starling_common/ActiveRegionDetector.hh>
#include "boost/test/unit_test.hpp"

BOOST_AUTO_TEST_SUITE( test_activeRegion )

typedef std::unique_ptr<IndelBuffer> IndelBufferPtr;

static IndelBufferPtr getIndelBufferPtr(const reference_contig_segment& ref)
{
    // fake starling options
    starling_options opt;
    opt.bam_seq_name = "chr1";
    opt.is_user_genome_size = true;
    opt.user_genome_size = ref.seq().size();

    starling_deriv_options dopt(opt,ref);

    const double maxDepth = 100.0;
    auto indelBufferPtr = IndelBufferPtr(new IndelBuffer(opt, dopt, ref));
    indelBufferPtr->registerSample(depth_buffer(), depth_buffer(), maxDepth);

    return indelBufferPtr;
}

// checks whether positions with consistent mismatches are marked as polymorphic sites
BOOST_AUTO_TEST_CASE( test_relaxMMDF )
{
    reference_contig_segment ref;
    ref.seq() = "TCTTT";

    auto indelBuffer = *getIndelBufferPtr(ref);

    const unsigned maxIndelSize = 50;
    auto detector = ActiveRegionDetector(ref, indelBuffer, maxIndelSize);

    const int depth = 50;

    const auto snvPos = std::set<pos_t>({0, 2, 3});


    pos_t refLength = (pos_t)ref.seq().length();

    // fake reading reads
    for (int alignId=0; alignId < depth; ++alignId)
    {
        for (pos_t pos(0); pos<refLength-1; ++pos)
        {
            // alternative allele frequency 0.5
            if ((alignId % 2) || (snvPos.find(pos) == snvPos.end()))
            {
                // No SNV
                detector.insertMatch(alignId, pos);
            }
            else
            {
                // SNV position
                detector.insertMismatch(alignId, pos, 'A');
            }
        }
    }

    for (pos_t pos(0); pos<refLength-1; ++pos)
    {
        detector.updateEndPosition(pos, false);
    }
    detector.updateEndPosition(refLength-1, true);


    // check if polySites are correctly set
    for (pos_t pos(0); pos<refLength-1; ++pos)
    {
        if (snvPos.find(pos) != snvPos.end())
        {
            // SNV
            BOOST_REQUIRE_EQUAL(detector.isPolymorphicSite(pos), true);
        }
        else
        {
            // No SNV
            BOOST_REQUIRE_EQUAL(detector.isPolymorphicSite(pos), false);
        }
    }
}

// Checks whether an indel is correctly confirmed in active regions
BOOST_AUTO_TEST_CASE( test_indelCandidacy )
{
    reference_contig_segment ref;
    ref.seq() = "TCTTT";

    auto indelBuffer = *getIndelBufferPtr(ref);

    const unsigned maxIndelSize = 50;
    auto detector = ActiveRegionDetector(ref, indelBuffer, maxIndelSize);

    const int sampleId = 0;
    const int depth = 50;

    const pos_t indelPos = 2;
    auto indelKey = IndelKey(indelPos, INDEL::INDEL, 0, "AG");

    pos_t refLength = (pos_t)ref.seq().length();

    // fake reading reads
    for (int alignId=0; alignId < depth; ++alignId)
    {
        for (pos_t pos(0); pos<refLength-1; ++pos)
        {
            detector.insertMatch(alignId, pos);

            if (pos == indelPos && (alignId % 2))
            {
                IndelObservation indelObservation;

                IndelObservationData indelObservationData;
                indelObservation.key = indelKey;
                indelObservationData.id = alignId;
                indelObservationData.iat = INDEL_ALIGN_TYPE::GENOME_TIER1_READ;
                indelObservation.data = indelObservationData;

                detector.insertIndel(sampleId, indelObservation);
            }
        }
    }

    for (pos_t pos(0); pos<refLength-1; ++pos)
    {
        detector.updateEndPosition(pos, false);
    }
    detector.updateEndPosition(refLength-1, true);

    const auto itr(indelBuffer.getIndelIter(indelKey));
    BOOST_REQUIRE_EQUAL(itr->second.isConfirmedInActiveRegion, true);
}

BOOST_AUTO_TEST_CASE( test_jumpingPositions )
{
    reference_contig_segment ref;
    ref.seq() = std::string(10000, 'A');

    // fake starling options
    starling_options opt;
    opt.bam_seq_name = "chr1";
    opt.is_user_genome_size = true;
    opt.user_genome_size = ref.seq().size();

    starling_deriv_options dopt(opt,ref);

    const double maxDepth = 100.0;
    auto indelBuffer = IndelBuffer(opt, dopt, ref);
    indelBuffer.registerSample(depth_buffer(), depth_buffer(), maxDepth);

    const unsigned maxIndelSize = 50;
    auto detector = ActiveRegionDetector(ref, indelBuffer, maxIndelSize);

    // fake reading reads
    const int depth = 50;
    const unsigned readLength = 100;

    const pos_t startPositions[] = {100, 7000};

    for (auto startPosition : startPositions)
    {
        pos_t endPosition = startPosition + readLength;
        for (int alignId=0; alignId < depth; ++alignId)
        {
            for (pos_t pos(startPosition); pos<endPosition; ++pos)
            {
                // SNVs at startPosition+10 and startPosition+12
                auto isSnvPosition = (pos == startPosition+10) || (pos == startPosition+12);
                if ((alignId % 2) && isSnvPosition)
                {
                    detector.insertMismatch(alignId, pos, 'G');
                }
                else
                {
                    detector.insertMatch(alignId, pos);
                }
            }
        }

        for (pos_t pos(startPosition); pos<endPosition; ++pos)
        {
            detector.updateEndPosition(pos, false);
            detector.updateStartPosition(pos - (readLength + maxIndelSize));
        }

        // check if polySites are correctly set
        BOOST_REQUIRE_EQUAL(detector.isPolymorphicSite(startPosition+10), true);
        BOOST_REQUIRE_EQUAL(detector.isPolymorphicSite(startPosition+12), true);
    }
}

BOOST_AUTO_TEST_SUITE_END()
