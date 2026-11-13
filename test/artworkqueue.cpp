// Language: en-GB. SPDX-License-Identifier: MPL-2.0
#include <foo_vis_milk2/artworkqueue.h>
#include <CppUnitTest.h>
#include <condition_variable>
#include <memory>
#include <thread>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace MilkDrop2
{
TEST_CLASS(ArtworkQueueTest)
{
public:
    TEST_METHOD(NewTrackRejectsLateArtworkAndClearsThePreviousImage)
    {
        Milk2Artwork::Queue queue;
        const auto old = queue.BeginTrack(true);
        Assert::IsTrue(queue.Publish(old, {{1,2,3}, L"old"}));
        const auto current = queue.BeginTrack(true);
        Assert::IsFalse(queue.Publish(old, {{9}, L"stale"}));
        Milk2Artwork::Image image;
        Assert::IsTrue(queue.Take(image)); Assert::IsTrue(image.bytes.empty());
        Assert::IsTrue(queue.Publish(current, {{4,5}, L"current"}));
        Assert::IsTrue(queue.Take(image)); Assert::AreEqual(size_t(2),image.bytes.size());
        Assert::IsTrue(image.path == L"current"); Assert::IsFalse(queue.Take(image));
    }
    TEST_METHOD(OnlyTheLatestFileRequestIsQueuedBehindAnActiveWorker)
    {
        Milk2Artwork::Queue queue; Milk2Artwork::FileJob job;
        auto generation=queue.BeginTrack(false);
        Assert::IsTrue(queue.QueueFile(generation,L"first")); Assert::IsTrue(queue.NextFile(job));
        Assert::IsTrue(job.path==L"first");
        generation=queue.BeginTrack(false); Assert::IsFalse(queue.QueueFile(generation,L"second"));
        generation=queue.BeginTrack(false); Assert::IsFalse(queue.QueueFile(generation,L"latest"));
        Assert::IsTrue(queue.NextFile(job)); Assert::IsTrue(job.path==L"latest");
        Assert::IsFalse(queue.NextFile(job));
        generation=queue.BeginTrack(false); Assert::IsTrue(queue.QueueFile(generation,L"restart"));
    }
    TEST_METHOD(ClosingAViewRejectsWorkerResultsWithoutNeedingTheViewToSurvive)
    {
        auto queue=std::make_shared<Milk2Artwork::Queue>();auto worker=queue;
        const auto generation=queue->BeginTrack(false);queue->Close();queue.reset();
        Assert::IsFalse(worker->IsCurrent(generation));
        Assert::IsFalse(worker->Publish(generation,{{1},L"late"}));
        Milk2Artwork::Image image; Assert::IsFalse(worker->Take(image));
        worker->Open(); const auto fresh=worker->BeginTrack(true);
        Assert::IsTrue(fresh!=generation); Assert::IsTrue(worker->Publish(fresh,{{2},L""}));
    }
    TEST_METHOD(EmbeddedNotificationsCannotReplaceCustomFileRequests)
    {
        Milk2Artwork::Queue queue;
        const auto embedded=queue.BeginTrack(true); Assert::IsTrue(queue.EmbeddedGeneration()==embedded);
        queue.BeginTrack(false); Assert::IsTrue(queue.EmbeddedGeneration()==0);
        Assert::IsFalse(queue.Publish(embedded,{{1},L""}));
        queue.Close(); Assert::IsTrue(queue.EmbeddedGeneration()==0);
    }
    TEST_METHOD(FailedTaskDispatchCanBeRetriedWithoutLosingNewTrackChanges)
    {
        Milk2Artwork::Queue queue;
        auto generation=queue.BeginTrack(false);Assert::IsTrue(queue.QueueFile(generation,L"first"));
        queue.WorkerFailed();generation=queue.BeginTrack(false);
        Assert::IsTrue(queue.QueueFile(generation,L"next"));
        Milk2Artwork::FileJob job;Assert::IsTrue(queue.NextFile(job));Assert::IsTrue(job.path==L"next");
    }
    TEST_METHOD(TrackUpdatesProceedWhileAnArtworkProviderIsBlocked)
    {
        auto queue=std::make_shared<Milk2Artwork::Queue>();
        const auto old=queue->BeginTrack(false);queue->QueueFile(old,L"slow-provider");
        std::mutex mutex;std::condition_variable condition;bool entered=false,release=false,staleAccepted=true;
        std::thread worker([&] {
            Milk2Artwork::FileJob job;queue->NextFile(job);
            {std::unique_lock<std::mutex> lock(mutex);entered=true;condition.notify_one();condition.wait(lock,[&]{return release;});}
            staleAccepted=queue->Publish(job.generation,{{1},L"slow-provider"});
        });
        {std::unique_lock<std::mutex> lock(mutex);condition.wait(lock,[&]{return entered;});}
        // This runs before releasing the blocked provider. No worker or file
        // completion is required in order to change tracks and clear old art.
        const auto current=queue->BeginTrack(true);
        const bool published=queue->Publish(current,{{2},L"current"});
        Milk2Artwork::Image image;const bool taken=queue->Take(image);
        {std::lock_guard<std::mutex> lock(mutex);release=true;}condition.notify_one();worker.join();
        Assert::IsTrue(published&&taken&&image.path==L"current");Assert::IsFalse(staleAccepted);
    }
};
}
