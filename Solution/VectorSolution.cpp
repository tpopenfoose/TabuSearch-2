#include "VectorSolution.hpp"
#include <algorithm>
#include <iostream>
#include <cassert>

namespace
{
    size_t hash(size_t first, size_t second, size_t third)
    {
        return std::hash<size_t>()(first) ^ std::hash<size_t>()(second) ^ std::hash<size_t>()(third);
    }
}

size_t VectorSolution::MoveHash::operator() (const IMove::AtomMove& atomMove) const
{
    return hash(atomMove.destination, atomMove.source, atomMove.diskId);
}

VectorSolution::VectorSolution(const TaskData& _data, const double _overheadsCoefficient)
    : data(_data)
    , distribution(data.getInitialDistribution())
    , matrixCapacity(_data.getNumberOfServers(), _data.getNumberOfTimes(), _data.getNumberOfResource())
    , thresholdOverheadsServer(data.getThresholdOverheads())
    , objectiveValue(0)
    , overheads(0)
    , overheadsCoefficient(_overheadsCoefficient)
    , serversColor(_data.getNumberOfServers(), Color::NONE)
{
    objectiveValue = fillObjectiveValueMatrix();
    paintServers();
}

VectorSolution::VectorSolution(const VectorSolution& other)
    : data(other.data)
    , distribution(other.distribution)
    , matrixCapacity(other.matrixCapacity)
    , thresholdOverheadsServer(other.thresholdOverheadsServer)
    , objectiveValue(other.objectiveValue)
    , overheads(other.overheads)
    , moveHistory(other.moveHistory)
    , overheadsCoefficient(other.overheadsCoefficient)
    , serversColor(other.serversColor)
{}

void VectorSolution::swap(VectorSolution &other)
{
    distribution.swap(other.distribution);
    std::swap(thresholdOverheadsServer, other.thresholdOverheadsServer);
    std::swap(objectiveValue, other.objectiveValue);
    std::swap(overheads, other.overheads);
    std::swap(overheadsCoefficient, other.overheadsCoefficient);
    std::swap(matrixCapacity, other.matrixCapacity);
    moveHistory.swap(other.moveHistory);
    serversColor.swap(other.serversColor);
}

VectorSolution& VectorSolution::operator=(const VectorSolution& rhs)
{
    if (&rhs != this)
    {
        VectorSolution tmp(rhs);
        swap(tmp);
    }
    return *this;
}

namespace
{

double antiHandlerOverheads(const double capacity, const double overheads, double startOverheads)
{
    if (capacity < 0 && (capacity + overheads) >= 0)
    {
        startOverheads -= std::abs(capacity);
    }
    else if (capacity + overheads < 0)
    {
        startOverheads -= overheads;
    }
    return startOverheads;
}

double handlerOverheads(const double capacity, const double overheads, double startOverheads)
{
    if (capacity <= 0)
    {
        startOverheads += overheads;
    }
    else if (capacity - overheads < 0)
    {
        startOverheads += (overheads - capacity);
    }
    return startOverheads;
}

}

double VectorSolution::getOverheads( const IMove::AtomMove& atomMove,
                                     std::unordered_set<IMove::AtomMove, MoveHash>* const appliedMoves,
                                     TwoDimensionalMatrix<double>* const thresholdOverheadsServer,
                                     double startOverheads) const
{
    auto previouslyMoveIterator = appliedMoves->find(atomMove);
    bool moveMeetPreviously = appliedMoves->end() != previouslyMoveIterator;
    for (size_t resource = 0; resource < data.getNumberOfResource(); ++resource)
    {
        double& sourceCapacity = thresholdOverheadsServer->get(atomMove.source, resource);
        double& destinationCapacity = thresholdOverheadsServer->get(atomMove.destination, resource);

        if (moveMeetPreviously)
        {
            // don't forget that move saves in reverse order
            const double eraseOverhead = data.getDiskCosts(TaskData::ERASE, atomMove.diskId, atomMove.destination, resource);
            const double insertOverhead = data.getDiskCosts(TaskData::INSERT, atomMove.diskId, atomMove.source, resource);

            // anti insert disk
            startOverheads = antiHandlerOverheads(sourceCapacity, insertOverhead, startOverheads);
            sourceCapacity += insertOverhead;

            // anti erase disk
            startOverheads = antiHandlerOverheads(destinationCapacity, eraseOverhead, startOverheads);
            destinationCapacity += eraseOverhead;
        }
        else
        {
            const double eraseOverhead = data.getDiskCosts(TaskData::ERASE, atomMove.diskId, atomMove.source, resource);
            const double insertOverhead = data.getDiskCosts(TaskData::INSERT, atomMove.diskId, atomMove.destination, resource);

            // erase
            startOverheads = handlerOverheads(sourceCapacity, eraseOverhead, startOverheads);
            sourceCapacity -= eraseOverhead;

            // insert
            startOverheads = handlerOverheads(destinationCapacity, insertOverhead, startOverheads);
            destinationCapacity -= insertOverhead;
        }
    }

    if (moveMeetPreviously)
    {
        // delete, because we undo this move
        appliedMoves->erase(previouslyMoveIterator);
    }
    else
    {
        // save inverse atom move
        appliedMoves->emplace(atomMove.source, atomMove.destination, atomMove.diskId);
    }

    return startOverheads;
}

double VectorSolution::getOverheads() const
{
    return overheads;
}

bool VectorSolution::moveIsCorrect(const IMove &move) const
{
    // fast fix
    auto atomMoves = move.getAtomMove();
    if (atomMoves.size() == 2) // swap move
    {
        IMove::AtomMove& atomMove1 = atomMoves[0];
        IMove::AtomMove& atomMove2 = atomMoves[1];
        if (
                (distribution[atomMove1.diskId].color == Color::RED && distribution[atomMove2.diskId].color == Color::RED)
                || (distribution[atomMove1.diskId].color == Color::YELLOW && distribution[atomMove2.diskId].color == Color::YELLOW)
           )
        {
            return true;
        }
    }

    for (auto atomMove : atomMoves)
    {
        assert(atomMove.source != atomMove.destination);
        assert(atomMove.source == distribution[atomMove.diskId].serverId);

        if (distribution[atomMove.diskId].color == Color::YELLOW && serversColor[atomMove.destination] == Color::YELLOW)
        {
            return false;
        }
        else if (distribution[atomMove.diskId].color == Color::RED)
        {
            return false;
        }
    }

    return true;
}

double VectorSolution::tryOnAtomMove(ThreeDimensionalMatrix<double> *matrixCapacity, double startObjectiveValue, const IMove::AtomMove& atomMove) const
{
    assert(atomMove.source != atomMove.destination);
    assert(atomMove.source == distribution[atomMove.diskId].serverId);
    assert(atomMove.source < data.getNumberOfServers() && atomMove.destination < data.getNumberOfServers());

    if (atomMove.source == atomMove.destination)
    {
        return startObjectiveValue;
    }

    // recalc objective value
    for (size_t time = 0; time < data.getNumberOfTimes(); ++time)
    {
        for (size_t resource = 0; resource < data.getNumberOfResource(); ++resource)
        {
            // for destination
            if (matrixCapacity->get(atomMove.destination, time, resource) <= 0)
            {
                double newValueWithDisk = matrixCapacity->get(atomMove.destination, time, resource) + data.getCapacity(atomMove.diskId, resource, time);
                if (newValueWithDisk > 0)
                {
                    startObjectiveValue += newValueWithDisk;
                }
            }
            else
            {
                startObjectiveValue += data.getCapacity(atomMove.diskId, resource, time);
            }
            matrixCapacity->get(atomMove.destination, time, resource) += data.getCapacity(atomMove.diskId, resource, time);

            // for source
            if (matrixCapacity->get(atomMove.source, time, resource) > 0)
            {
                double newValueWithoutDisk = matrixCapacity->get(atomMove.source, time, resource) - data.getCapacity(atomMove.diskId, resource, time);
                if (newValueWithoutDisk <= 0)
                {
                    startObjectiveValue -= matrixCapacity->get(atomMove.source, time, resource);
                }
                else
                {
                    startObjectiveValue -= data.getCapacity(atomMove.diskId, resource, time);
                }
            }
            matrixCapacity->get(atomMove.source, time, resource) -= data.getCapacity(atomMove.diskId, resource, time);
        }
    }

    return startObjectiveValue;
}

double VectorSolution::tryOnMove(const IMove& move) const
{
    auto matrixCapacityCopy = matrixCapacity;
    double objectiveValueCopy = objectiveValue;
    double overheadsCopy = overheads;
    auto thresholdOverheadsServerCopy = thresholdOverheadsServer;
    auto appliedMovesCopy = appliedMoves;

    for (auto atomMove : move.getAtomMove())
    {
        assert(atomMove.source != atomMove.destination);
        assert(atomMove.source == distribution[atomMove.diskId].serverId);
        assert(atomMove.source < data.getNumberOfServers() && atomMove.destination < data.getNumberOfServers());

        double newObjectiveValue = tryOnAtomMove(&matrixCapacityCopy, objectiveValueCopy, atomMove);
        double newOverheads = getOverheads(atomMove, &appliedMovesCopy, &thresholdOverheadsServerCopy, overheadsCopy);
        objectiveValueCopy = newObjectiveValue;
        overheadsCopy = newOverheads;
    }

    return objectiveValueCopy + overheadsCoefficient * overheadsCopy;
}

void VectorSolution::applyMove(const IMove& move)
{
    for (const auto atomMove : move.getAtomMove())
    {
        assert(atomMove.source != atomMove.destination);
        assert(atomMove.source == distribution[atomMove.diskId].serverId);
        assert(atomMove.source < data.getNumberOfServers() && atomMove.destination < data.getNumberOfServers());

        moveHistory.push_back(atomMove);
        objectiveValue = tryOnAtomMove(&matrixCapacity, objectiveValue, atomMove);

        // overheads
        overheads = getOverheads(atomMove, &appliedMoves, &thresholdOverheadsServer, overheads);

        moveDisk(atomMove.destination, atomMove.source, atomMove.diskId);
    }

    // std::cout << objectiveValue << " overheads " << (overheads) << std::endl;
}

double VectorSolution::getObjectiveValue() const
{
    return objectiveValue + overheadsCoefficient * overheads;
}

double VectorSolution::fillObjectiveValueMatrix()
{
    for (size_t server = 0; server < data.getNumberOfServers(); ++server)
    {
        for (size_t time = 0; time < data.getNumberOfTimes(); ++time)
        {
            for (size_t resource = 0; resource < data.getNumberOfResource(); ++resource)
            {
                matrixCapacity.get(server, time, resource) = -1 * data.getThresholdCapacity(server, resource);
            }
        }
    }

    for (size_t diskId = 0; diskId < distribution.size(); ++diskId)
    {
        for (size_t time = 0; time < data.getNumberOfTimes(); ++time)
        {
            for (size_t resource = 0; resource < data.getNumberOfResource(); ++resource)
            {
                matrixCapacity.get(distribution[diskId].serverId, time, resource) += data.getCapacity(diskId, resource, time);
            }
        }
    }

    double objectiveValue = 0;
    for (size_t server = 0; server < data.getNumberOfServers(); ++server)
    {
        for (size_t time = 0; time < data.getNumberOfTimes(); ++time)
        {
            for (size_t resource = 0; resource < data.getNumberOfResource(); ++resource)
            {
                if (matrixCapacity.get(server, time, resource) > 0)
                {
                    objectiveValue += matrixCapacity.get(server, time, resource);
                }
            }
        }
    }

    return objectiveValue;
}

void VectorSolution::moveDisk(size_t destination, size_t source, size_t diskId)
{
    assert(distribution[diskId].serverId == source);
    distribution[diskId].serverId = destination;
}

std::vector<TaskData::Disk> VectorSolution::getDistribution() const
{
    return distribution;
}

std::vector<IMove::AtomMove> VectorSolution::getMoveHistory() const
{
    return moveHistory;
}

void VectorSolution::setOverheadsCoefficient(const double value)
{
    overheadsCoefficient = value;
}

double VectorSolution::getOverheadsCoefficient()
{
    return overheadsCoefficient;
}

size_t VectorSolution::getServerForDisk(size_t diskId) const
{
    assert(diskId < distribution.size());
    return distribution[diskId].serverId;
}

TaskData::Color VectorSolution::getServerColor(const size_t server) const
{
    assert(server < data.getNumberOfServers());
    return serversColor[server];
}

TaskData::Color VectorSolution::getDiskColor(const size_t disk) const
{
    assert(disk < distribution.size());
    return distribution[disk].color;
}

void VectorSolution::paintServers()
{
    for (size_t diskId = 0; diskId < data.getNumberOfDisks(); ++diskId)
    {
        switch (distribution[diskId].color)
        {
        case Color::RED:
            serversColor[distribution[diskId].serverId] = Color::RED;
            break;
        case Color::YELLOW:
            if (serversColor[distribution[diskId].serverId] != Color::RED)
            {
                serversColor[distribution[diskId].serverId] = Color::YELLOW;
            }
            break;
        case Color::GREEN:
            if (serversColor[distribution[diskId].serverId] == Color::NONE)
            {
                serversColor[distribution[diskId].serverId] = Color::GREEN;
            }
            break;
        default:
            assert(false);
            break;
        }
    }
}

std::ostream& operator<<(std::ostream& outStream, const VectorSolution& solution)
{
    outStream << "Objective value = " << solution.getObjectiveValue() << '\n';

    // numbering from zero -> numbering from one
    auto distribution = solution.getDistribution();
    for (auto server : distribution)
    {
        outStream << server.serverId + 1 << ' ';
    }
    outStream << '\n';

    for (auto atomMove : solution.getMoveHistory())
    {
        outStream << atomMove << '\n';
    }

    return outStream;
}
