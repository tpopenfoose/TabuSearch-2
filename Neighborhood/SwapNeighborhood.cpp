#include "SwapNeighborhood.hpp"

#include "Move/CompoundMove.hpp"
#include <vector>

std::vector<std::unique_ptr<IMove> > SwapNeighborhood::getMoves(const ISolution& solution) const
{
    auto distrbution = solution.getDistribution();
    std::vector< std::unique_ptr<IMove> > result;

    for (size_t i = 0; i < distrbution.size(); ++i)
    {
        for (size_t j = i; j < distrbution.size(); ++j)
        {
            if (distrbution[i].serverId != distrbution[j].serverId)
            {
                std::vector<IMove::AtomMove> moves;
                moves.emplace_back(distrbution[j].serverId, distrbution[i].serverId, i);
                moves.emplace_back(distrbution[i].serverId, distrbution[j].serverId, j);

                std::unique_ptr<IMove> move(new CompoundMove(std::move(moves)));
                if (solution.moveIsCorrect(*move))
                {
                    result.emplace_back(std::move(move));
                }
            }
        }
    }

    return result;
}
