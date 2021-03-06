#include "dedupe.hpp"
#include "util.hpp"

#include <ROOT/RDataFrame.hxx>
#include <ROOT/RVec.hxx>
#include <ROOT/RSnapshotOptions.hxx>
#include <boost/functional/hash.hpp>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
bool seen(const unsigned run, const ULong64_t event)
{
    static std::mutex mtx;
    static std::unordered_set<std::pair<unsigned, ULong64_t>, boost::hash<std::pair<unsigned, ULong64_t>>> seen_events{};

    std::scoped_lock lock{mtx};
    return seen_events.emplace(run, event).second;
}
} // namespace

void dedupe()//int argc, char* argv[])
{
    // No faster (and perhaps even slower) to multi thread.
    // Bottleneck as only one thread can access the map at a time
    // ROOT::EnableImplicitMT();

    const std::vector<std::string> files{"/data/nanoAOD_2017/mumuRun2017B/*.root",
                                         "/data/nanoAOD_2017/muRun2017B/*.root"};
    ROOT::RDataFrame d{"Events", files};

    auto d_dup = d.Filter(seen, {"run", "event"}, "Event deduplicator");

    ROOT::RDF::RSnapshotOptions opts;
    opts.fCompressionAlgorithm = ROOT::kLZ4;
    opts.fCompressionLevel = 4;
    d_dup.Snapshot("Events", "deduplicated.root", util::apply_blacklist(d_dup.GetColumnNames()), opts);

    auto allCutsReport{d.Report()};
    const auto cut{allCutsReport->At("Event deduplicator")};
    std::cout << "Found " << cut.GetAll() - cut.GetPass() << " duplicated events" << std::endl;
}
