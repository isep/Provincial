#include "Main.h"



const bool fastTestMode = false;

String TestPlayer::VisualizationDescription(const Vector<Card*> &supplyCards, bool ignoreBuyID, bool ignoreCounts) const
{

    const BuyAgendaMenu *menu = dynamic_cast<const BuyAgendaMenu*>(&p->Agenda());
    BuyMenu m = menu->GetMenu();
    m.Cleanup();
    m.Merge();

    int maxCost = 999;
    String result = "e" + String(m.estateBuyThreshold) + "-d" + String(m.duchyBuyThreshold) + "-p" + String(m.provinceBuyThreshold) + "-";
    for(UINT index = 0; index < m.entries.Length(); index++)
    {
        const BuyMenuEntry &curEntry = m.entries[index];
        //if(curEntry.count > 0 && curEntry.c->cost < maxCost && (ignoreBuyID || buyID[supplyCards.FindFirstIndex(curEntry.c)] == '1') && curEntry.c->name != "estate")
        if(curEntry.count > 0 && (curEntry.c->name == "peddler" || curEntry.c->cost < maxCost))
        {
            if(ignoreCounts) result += curEntry.c->name + "|";
            else result += curEntry.c->name + "@" + String(curEntry.count) + "|";
        }
        if(curEntry.count >= 15)
        {
            maxCost = curEntry.c->cost;
        }
    }
    result.PopEnd();
    return result;
}

String TestPlayer::VisualizationDescriptionDecisionStrategy() const
{
	const DecisionStrategy *strat = dynamic_cast<const DecisionStrategy*>(&p->Agenda());
	//strat->SaveDecisionWeightsToFile("decision_" + result);
	return strat->getStringInfo();
}

void TestChamberTask::Run(ThreadLocalStorage *threadLocalStorage)
{
    //
    // Threads sometime run in lock-step: we could try to kick them out of it by staggered sleeping
    //
    //int randomSeed = Utility::Hash32((int)&threadLocalStorage);
    //Sleep(randomSeed % 3 + rand() % 3);


	//TODO:

    *result = chamber->Test(*cards, params, false);
}

void TestChamber::FreeMemory()
{
    _pool.FreeMemory();
    _leaders.FreeMemory();
    _bestLeaderHistory.FreeMemory();
    _allLeaderHistory.FreeMemory();
    _allLeaderHistoryHashes.FreeMemory();
    _supplyCards.FreeMemory();
    _leaderWeights.FreeMemory();
    _generation = 1;
}


void TestChamber::AssignBuyIDs(const Grid<TestResult> &poolResults)
{
    const UINT supplyCount = _supplyCards.Length();
    for(UINT poolIndex = 0; poolIndex < _pool.Length(); poolIndex++)
    {
        String curID = "";
        for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++)
        {
            char c = '0';
            for(UINT leaderIndex = 0; leaderIndex < _leaders.Length(); leaderIndex++)
            {
                if(_supplyCards[supplyIndex]->expansion == "core" || poolResults(poolIndex, leaderIndex).buyRatio[supplyIndex] > 0.25) c = '1';
            }
            curID.PushEnd(c);
        }
        _pool[poolIndex]->buyID = curID;
    }
}

void TestChamber::AssignNewLeaders(const CardDatabase &cards, PlayerType playerType)
{
    _leaders.FreeMemory();
    const UINT supplyCount = _supplyCards.Length();

    Vector<String> leaderBuyIDs;
    Vector<String> leaderMenuIDs;
    Vector<int> leaderSupplyCounters(supplyCount, 0);

    //
    // To make for more diverse leaders, certain cards are prevented from occuring in more than half of the leader buy orders
    //
    for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++)
    {
        Card *c = _supplyCards[supplyIndex];
        if(c->strength == -1) leaderSupplyCounters[supplyIndex] = _parameters.leaderCount;
        else leaderSupplyCounters[supplyIndex] = Math::Ceiling((double)_parameters.leaderCount / 2.0);
        //leaderSupplyCounters[supplyIndex] = Utility::Bound(leaderSupplyCounters[supplyIndex], 3, 6);
    }

    while(_leaders.Length() < _parameters.leaderCount)
    {
        bool newLeaderFound = false;
        for(UINT poolIndex = 0; poolIndex < _pool.Length() && !newLeaderFound; poolIndex++)
        {
            TestPlayer *curPlayer = _pool[poolIndex];
			if (( _trainingType == TRAINING_DECISIONS && !_leaders.Contains(curPlayer)) || !leaderBuyIDs.Contains(curPlayer->buyID) && !leaderMenuIDs.Contains(curPlayer->VisualizationDescription(_supplyCards, true, true)))
            {
                bool overusedCardFound = false;
                for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++)
                {
                    if(curPlayer->buyID[supplyIndex] == '1' && leaderSupplyCounters[supplyIndex] <= 0) overusedCardFound = true;
                }
				if (!overusedCardFound || _trainingType == TRAINING_DECISIONS)
                {
                    newLeaderFound = true;
                    _leaders.PushEnd(curPlayer);
                    leaderBuyIDs.PushEnd(curPlayer->buyID);
                    leaderMenuIDs.PushEnd(curPlayer->VisualizationDescription(_supplyCards, true, true));

                    for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++)
                    {
                        if(_supplyCards[supplyIndex]->expansion != "core" && curPlayer->buyID[supplyIndex] == '1') leaderSupplyCounters[supplyIndex]--;
                    }
                }
            }
        }
        if(!newLeaderFound)
        {
            Console::WriteLine("No leader found!");


			TestPlayer *newPlayer;
			if (playerType == HEURISTIC_PLAYER)
				newPlayer = new TestPlayer(new PlayerHeuristic(new BuyAgendaMenu(cards, _gameOptions)));
			else if (playerType == STATEINFORMED_PLAYER){
				newPlayer = new TestPlayer(new PlayerStateInformed(new DecisionStrategy(cards, _gameOptions)));
			}else{	newPlayer = NULL;}
			
			if (_trainingType==TRAINING_DECISIONS) {
				newPlayer->p = newPlayer->p->MutateOnlyDecisions(cards, _gameOptions);
			}
			else if (_trainingType == TRAINING_BUYS){
				newPlayer->p = newPlayer->p->MutateOnlyBuys(cards, _gameOptions);
			}
			else{
				newPlayer->p = newPlayer->p->Mutate(cards, _gameOptions);
			}
            _leaders.PushEnd(newPlayer);
        }
    }
}

void TestChamber::GenerateNewPool(const CardDatabase &cards, PlayerType playerType)
{
    _pool.FreeMemory();
    for(UINT leaderIndex = 0; leaderIndex < _leaders.Length(); leaderIndex++) _pool.PushEnd(_leaders[leaderIndex]);

    const bool useMutationPool = true;
    if(useMutationPool)
    {
        while(_pool.Length() < _parameters.poolSize)
        {
            TestPlayer *parent = _leaders.RandomElement();
			TestPlayer *mutatedChild;
			if (_trainingType == TRAINING_DECISIONS) {
				mutatedChild = new TestPlayer(parent->p->MutateOnlyDecisions(cards, _gameOptions));
			}
			else if (_trainingType == TRAINING_BUYS){
				mutatedChild = new TestPlayer(parent->p->MutateOnlyBuys(cards, _gameOptions));
			}
			else {
				mutatedChild = new TestPlayer(parent->p->Mutate(cards, _gameOptions));
			}

            while(rnd() >= _parameters.mutationTerminationProbability)
            {
				if (_trainingType == TRAINING_DECISIONS) {
					mutatedChild = new TestPlayer(parent->p->MutateOnlyDecisions(cards, _gameOptions));
				}
				else if (_trainingType == TRAINING_BUYS){
					mutatedChild = new TestPlayer(parent->p->MutateOnlyBuys(cards, _gameOptions));
				}
				else{
					mutatedChild = new TestPlayer(parent->p->Mutate(cards, _gameOptions));
				}
            }

            _pool.PushEnd(mutatedChild);
        }
    }

            
				}

Grid<TestResult> TestChamber::RunAllPairsTests(const CardDatabase &cards, const Vector<TestPlayer*> &playersA, const Vector<TestPlayer*> &playersB, UINT minGameCount, UINT maxGameCount)
{
    Grid<TestResult> allResults(playersA.Length(), playersB.Length());

    TestParameters params;
    params.minGameCount = minGameCount;
    params.maxGameCount = maxGameCount;
    params.options = _gameOptions;

    if(fastTestMode)
    {
        params.minGameCount = 10;
        params.maxGameCount = 10;
    }

    TaskList<WorkerThreadTask*> tasks;
    for(UINT playerAIndex = 0; playerAIndex < playersA.Length(); playerAIndex++)
    {
        for(UINT playerBIndex = 0; playerBIndex < playersB.Length(); playerBIndex++)
        {
            TestChamberTask *newTask = new TestChamberTask;
            newTask->chamber = this;
            newTask->cards = &cards;
            newTask->params = params;
            newTask->params.players[0] = playersA[playerAIndex]->p;
            newTask->params.players[1] = playersB[playerBIndex]->p;
            newTask->result = &allResults(playerAIndex, playerBIndex);
            tasks.InsertTask(newTask);
        }
    }

    ThreadPool threadPool;
    threadPool.Init(7);
    threadPool.Go(tasks);

    return allResults;
}

void TestChamber::ComputeCounters(const CardDatabase &cards, const String &filename)
{
    ofstream file(filename.CString());
    file << "Counters" << endl;

    Grid<TestResult> results = RunAllPairsTests(cards, _leaders, _allLeaderHistory, _parameters.visualizationGameCount, _parameters.visualizationGameCount);

    file << endl << "Kingdom cards:\t" << _gameOptions.ToString() << endl;

    const UINT supplyCount = _supplyCards.Length();
    
    file << endl << "Leaders:\t" << _leaders.Length() << endl;

    for(UINT leaderIndex = 0; leaderIndex < _leaders.Length(); leaderIndex++)
    {
        file << endl << "Leader" << leaderIndex << "\t0\t" << _leaders[leaderIndex]->VisualizationDescription(_supplyCards, true) << endl;

        Vector< pair<TestPlayer*, double> > players;
        for(UINT playerIndex = 0; playerIndex < _allLeaderHistory.Length(); playerIndex++)
        {
            players.PushEnd(make_pair(_allLeaderHistory[playerIndex], results(leaderIndex, playerIndex).winRatio[0]));
        }

        players.Sort([](const pair<TestPlayer*, double> &a, const pair<TestPlayer*, double> &b) { return a.second < b.second; } );

        for(int playerIndex = 0; playerIndex < Math::Min(int(_allLeaderHistory.Length()), 20); playerIndex++)
        {
            Console::WriteLine("Counter" + String(playerIndex) + "\t" + String(results(leaderIndex, 0).winRatio[1]));
            file << "Opponent " << playerIndex << ":\t" << String((players[playerIndex].second - 0.5) * 200.0) << "\t" << players[playerIndex].first->VisualizationDescription(_supplyCards, true) << endl;
        }
    }
}

void TestChamber::ComputeProgression(const CardDatabase &cards, TestPlayer *leader, const Vector<TestPlayer*> &players, const String &filename)
{
    const UINT leaderCount = players.Length();
    
    Console::WriteLine("Generating progression comparison for generation " + String(_generation) + _metaSuffix);

    ofstream file(filename.CString());
    file << "Progression" << endl;

    Vector<TestPlayer*> leaderList(1, leader);
    Grid<TestResult> results = RunAllPairsTests(cards, players, leaderList, _parameters.visualizationGameCount, _parameters.visualizationGameCount);

    file << endl << "Kingdom cards:\t" << _gameOptions.ToString() << endl;

    const UINT supplyCount = _supplyCards.Length();
    
    for(UINT leaderIndexA = 0; leaderIndexA < leaderCount; leaderIndexA++)
    {
        String curID = "";
        for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++)
        {
            char c = '0';
            if(_supplyCards[supplyIndex]->expansion == "core" || results(leaderIndexA, 0).buyRatio[supplyIndex] > 0.25) c = '1';
            curID.PushEnd(c);
        }
        players[leaderIndexA]->buyID = curID;
    }

    file << endl << "Leader\t0\t" << players.Last()->VisualizationDescription(_supplyCards, true) << endl;

    file << endl << "Opponents:\t" << leaderCount << endl;
    for(UINT leaderIndex = 0; leaderIndex < leaderCount; leaderIndex++)
    {
        //Console::WriteLine("Opponent " + String(leaderIndex) + ": " + String(results(leaderIndex, 0).winRatio[1]));
        file << "Opponent " << leaderIndex << ":\t" << String((results(leaderIndex, 0).winRatio[1] - 0.5) * 200.0) << "\t" << players[leaderIndex]->VisualizationDescription(_supplyCards, true) << endl;
    }

    //file << endl << "Full description" << endl;
    //for(UINT leaderIndex = 0; leaderIndex < leaderCount; leaderIndex++)
    //{
    //    file << "Opponent " << leaderIndex << ":\t" << players[leaderIndex]->p->ControllerName() << endl;
    //}
}

void TestChamber::ComputeProgressionDecisions(const CardDatabase &cards, TestPlayer *leader, const Vector<TestPlayer*> &players, const String &filename)
{
	const UINT leaderCount = players.Length();

	Console::WriteLine("Generating decisions progression comparison for generation " + String(_generation) + _metaSuffix);

	ofstream file(filename.CString());
	file << "Progression Decisions" << endl;

	Vector<TestPlayer*> leaderList(1, leader);
	Grid<TestResult> results = RunAllPairsTests(cards, players, leaderList, _parameters.visualizationGameCount, _parameters.visualizationGameCount);

	file << endl << "Kingdom cards:\t" << _gameOptions.ToString() << endl;

	const UINT supplyCount = _supplyCards.Length();

	for (UINT leaderIndexA = 0; leaderIndexA < leaderCount; leaderIndexA++)
	{
		String curID = "";
		for (UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++)
		{
			char c = '0';
			if (_supplyCards[supplyIndex]->expansion == "core" || results(leaderIndexA, 0).buyRatio[supplyIndex] > 0.25) c = '1';
			curID.PushEnd(c);
		}
		players[leaderIndexA]->buyID = curID;
	}

	file << endl << "Leader\t0" << "\t" << players.Last()->VisualizationDescription(_supplyCards, true) << "\t" << players.Last()->VisualizationDescriptionDecisionStrategy() << endl;

	file << endl << "Opponents:\t" << leaderCount << endl;
	for (UINT leaderIndex = 0; leaderIndex < leaderCount; leaderIndex++)
	{
		//Console::WriteLine("Opponent " + String(leaderIndex) + ": " + String(results(leaderIndex, 0).winRatio[1]));
		file << "Opponent " << leaderIndex << ":\t" << String((results(leaderIndex, 0).winRatio[1] - 0.5) * 200.0) << "\t" << players[leaderIndex]->VisualizationDescription(_supplyCards, true) << "\t" << players[leaderIndex]->VisualizationDescriptionDecisionStrategy() << endl;
	}

	//file << endl << "Full description" << endl;
	//for(UINT leaderIndex = 0; leaderIndex < leaderCount; leaderIndex++)
	//{
	//    file << "Opponent " << leaderIndex << ":\t" << players[leaderIndex]->p->ControllerName() << endl;
	//}
}

void TestChamber::ComputeLeaderboard(const CardDatabase &cards, const Vector<TestPlayer*> &players, const String &filename, UINT gameCount)
{
    Console::WriteLine("Generating leaderboard comparison for generation " + String(_generation) + _metaSuffix);

    const UINT leaderCount = players.Length();
    
    ofstream file(filename.CString());
    Grid<TestResult> results = RunAllPairsTests(cards, players, players, gameCount, gameCount);

    file << "Leaderboard" << endl;
    file << endl << "Kingdom cards:\t" << _gameOptions.ToString() << endl;

    const UINT supplyCount = _supplyCards.Length();
    
    for(UINT leaderIndexA = 0; leaderIndexA < leaderCount; leaderIndexA++)
    {
        String curID = "";
        for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++)
        {
            char c = '0';
            for(UINT leaderIndexB = 0; leaderIndexB < leaderCount; leaderIndexB++)
            {
                if(_supplyCards[supplyIndex]->expansion == "core" || results(leaderIndexA, leaderIndexB).buyRatio[supplyIndex] > 0.25) c = '1';
            }
            curID.PushEnd(c);
        }
        players[leaderIndexA]->buyID = curID;
    }

    file << endl << "Leaders:\t" << leaderCount << endl;
    for(UINT leaderIndexA = 0; leaderIndexA < leaderCount; leaderIndexA++)
    {
        double averageWinRatio = 0.0;
        for(UINT leaderIndexB = 0; leaderIndexB < leaderCount; leaderIndexB++) if(leaderIndexA != leaderIndexB) averageWinRatio += results(leaderIndexA, leaderIndexB).winRatio[0];
        if(leaderCount > 1) averageWinRatio /= double(leaderCount - 1);

        file << "Leader " << leaderIndexA << ":\t" << (averageWinRatio - 0.5) * 200.0 << "\t" << players[leaderIndexA]->VisualizationDescription(_supplyCards, true) << endl;
    }

    //file << endl << "Full description" << endl;
    //for(UINT leaderIndex = 0; leaderIndex < leaderCount; leaderIndex++)
    //{
    //    file << "Leader " << leaderIndex << ":\t" << players[leaderIndex]->p->ControllerName() << endl;
    //}

    file << endl << "Tournament" << endl;
    
    file << "player";
    for(UINT leaderIndexA = 0; leaderIndexA < leaderCount; leaderIndexA++) file << "\t" << leaderIndexA;
    file << endl;

    for(UINT leaderIndexA = 0; leaderIndexA < leaderCount; leaderIndexA++)
    {
        file << leaderIndexA;
        for(UINT leaderIndexB = 0; leaderIndexB < leaderCount; leaderIndexB++)
        {
            file << "\t" << (results(leaderIndexA, leaderIndexB).winRatio[0] - 0.5) * 200.0;
        }
        file << endl;
    }

    file << endl << "Buy Statistics" << endl;
    
    file << "player";
    for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++) file << "\t" << _supplyCards[supplyIndex]->name;
    file << endl;

    for(UINT leaderIndexA = 0; leaderIndexA < leaderCount; leaderIndexA++)
    {
        file << leaderIndexA;
        for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++)
        {
            double value = 0.0;
            for(UINT leaderIndexB = 0; leaderIndexB < leaderCount; leaderIndexB++)
            {
                value += results(leaderIndexA, leaderIndexB).buyRatio[supplyIndex];
            }
            file << "\t" << value / double(leaderCount);
        }
        file << endl;
    }
}

void TestChamber::ComputeLeaderboardDecisions(const CardDatabase &cards, const Vector<TestPlayer*> &players, const String &filename, UINT gameCount)
{
	Console::WriteLine("Generating decisions leaderboard comparison for generation " + String(_generation) + _metaSuffix);

	const UINT leaderCount = players.Length();

	ofstream file(filename.CString());
	Grid<TestResult> results = RunAllPairsTests(cards, players, players, gameCount, gameCount);

	file << "Leaderboard Decisions" << endl;
	file << endl << "Kingdom cards:\t" << _gameOptions.ToString() << endl;

	const UINT supplyCount = _supplyCards.Length();

	for (UINT leaderIndexA = 0; leaderIndexA < leaderCount; leaderIndexA++)
	{
		String curID = "";
		for (UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++)
		{
			char c = '0';
			for (UINT leaderIndexB = 0; leaderIndexB < leaderCount; leaderIndexB++)
			{
				if (_supplyCards[supplyIndex]->expansion == "core" || results(leaderIndexA, leaderIndexB).buyRatio[supplyIndex] > 0.25) c = '1';
			}
			curID.PushEnd(c);
		}
		players[leaderIndexA]->buyID = curID;
	}

	file << endl << "Buy order:\t" << players[0]->VisualizationDescription(_supplyCards, true) << endl;

	file << endl << "Tournament" << endl;

	file << "player";
	for (UINT leaderIndexA = 0; leaderIndexA < leaderCount; leaderIndexA++) file << "\t" << leaderIndexA;
	file << endl;

	for (UINT leaderIndexA = 0; leaderIndexA < leaderCount; leaderIndexA++)
	{
		file << leaderIndexA;
		for (UINT leaderIndexB = 0; leaderIndexB < leaderCount; leaderIndexB++)
		{
			file << "\t" << (results(leaderIndexA, leaderIndexB).winRatio[0] - 0.5) * 200.0;
		}
		file << endl;
	}

	file << endl << "Leaders:\t" << leaderCount << endl;
	for (UINT leaderIndexA = 0; leaderIndexA < leaderCount; leaderIndexA++)
	{
		double averageWinRatio = 0.0;
		for (UINT leaderIndexB = 0; leaderIndexB < leaderCount; leaderIndexB++) if (leaderIndexA != leaderIndexB) averageWinRatio += results(leaderIndexA, leaderIndexB).winRatio[0];
		if (leaderCount > 1) averageWinRatio /= double(leaderCount - 1);

		file << "Leader " << leaderIndexA << ":\t" << (averageWinRatio - 0.5) * 200.0 << "\t" << players[leaderIndexA]->VisualizationDescription(_supplyCards, true) << "\t" << players[leaderIndexA]->VisualizationDescriptionDecisionStrategy() << endl;
	}

	//file << endl << "Full description" << endl;
	//for(UINT leaderIndex = 0; leaderIndex < leaderCount; leaderIndex++)
	//{
	//    file << "Leader " << leaderIndex << ":\t" << players[leaderIndex]->p->ControllerName() << endl;
	//}
}




Grid<TestResult> TestChamber::TestBuyPool(const CardDatabase &cards, const String &filename)
{
    Grid<TestResult> allResults = RunAllPairsTests(cards, _pool, _leaders, 200,_parameters.standardGameCount);
    const UINT supplyCount = allResults(0, 0).buyRatio.Length();
    const UINT leaderCount = _leaders.Length();

    double score = 0.0;
    for(UINT playerIndex = 0; playerIndex < _pool.Length(); playerIndex++)
    {
        double rating = 0.0;
        for(UINT leaderIndex = 0; leaderIndex < leaderCount; leaderIndex++) rating += allResults(playerIndex, leaderIndex).winRatio[0] * _leaderWeights[leaderIndex];
        _pool[playerIndex]->rating = rating;
    }

    ofstream file(filename.CString());
    file << "Generation" << endl;
    file << endl << "Kingdom cards:\t" << _gameOptions.ToString() << endl;

    file << endl << "Leaders:\t" << leaderCount << endl;
    for(UINT leaderIndex = 0; leaderIndex < leaderCount; leaderIndex++)
    {
        file << "Leader " << leaderIndex << ":\t" << (_leaders[leaderIndex]->rating - 0.5) * 200.0 << "\t" << _leaders[leaderIndex]->VisualizationDescription(_supplyCards, true, false) << endl;
    }

    /*file << "Leaders" << endl;
    file << "Index\tRating\tName" << endl;
    for(UINT leaderIndex = 0; leaderIndex < _leaders.Length(); leaderIndex++)
    {
        file << leaderIndex << '\t' << _leaders[leaderIndex]->rating << '\t' << _leaders[leaderIndex]->p->ControllerName() << endl;
    }*/

    file << endl << "Index\tRating";
    for(UINT leaderIndex = 0; leaderIndex < _leaders.Length(); leaderIndex++) file << "\tL" << leaderIndex;
    for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++) file << '\t' << _supplyCards[supplyIndex]->PrettyName();
    file << "\tName" << endl;
    for(UINT playerIndex = 0; playerIndex < _pool.Length(); playerIndex++)
    {
        file << playerIndex << '\t' << _pool[playerIndex]->rating;

        Vector<double> buyRatio(supplyCount, 0.0);
        for(UINT leaderIndex = 0; leaderIndex < _leaders.Length(); leaderIndex++)
        {
            for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++) buyRatio[supplyIndex] += allResults(playerIndex, leaderIndex).buyRatio[supplyIndex];
            file << '\t' << allResults(playerIndex, leaderIndex).winRatio[0];
        }

        for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++) file << '\t' << buyRatio[supplyIndex];

        file << '\t' << _pool[playerIndex]->p->ControllerName() << endl;
    }

    return allResults;
}

Grid<TestResult> TestChamber::TestDecisionPool(const CardDatabase &cards, const String &filename)
{
	Grid<TestResult> allResults = RunAllPairsTests(cards, _pool, _leaders, 200, _parameters.standardGameCount);
	const UINT supplyCount = allResults(0, 0).buyRatio.Length();
	const UINT leaderCount = _leaders.Length();

	double score = 0.0;
	for (UINT playerIndex = 0; playerIndex < _pool.Length(); playerIndex++)
	{
		double rating = 0.0;
		for (UINT leaderIndex = 0; leaderIndex < leaderCount; leaderIndex++) rating += allResults(playerIndex, leaderIndex).winRatio[0] * _leaderWeights[leaderIndex];
		_pool[playerIndex]->rating = rating;
	}

	ofstream file(filename.CString());
	file << "Generation Decisions" << endl;
	file << endl << "Kingdom cards:\t" << _gameOptions.ToString() << endl;

	file << endl << "Leaders:\t" << leaderCount << endl;
	for (UINT leaderIndex = 0; leaderIndex < leaderCount; leaderIndex++)
	{
		file << "Leader " << leaderIndex << ":\t" << (_leaders[leaderIndex]->rating - 0.5) * 200.0 << "\t" << _leaders[leaderIndex]->VisualizationDescription(_supplyCards, true) << "\t" << _leaders[leaderIndex]->VisualizationDescriptionDecisionStrategy() << endl;
	}

	/*file << "Leaders" << endl;
	file << "Index\tRating\tName" << endl;
	for(UINT leaderIndex = 0; leaderIndex < _leaders.Length(); leaderIndex++)
	{
	file << leaderIndex << '\t' << _leaders[leaderIndex]->rating << '\t' << _leaders[leaderIndex]->p->ControllerName() << endl;
	}*/

	file << endl << "Index\tRating";
	for (UINT leaderIndex = 0; leaderIndex < _leaders.Length(); leaderIndex++) file << "\tL" << leaderIndex;
	//for (UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++) file << '\t' << _supplyCards[supplyIndex]->PrettyName();
	file << "\tName" << endl;
	for (UINT playerIndex = 0; playerIndex < _pool.Length(); playerIndex++)
	{
		file << playerIndex << '\t' << _pool[playerIndex]->rating;

		//Vector<double> buyRatio(supplyCount, 0.0);
		for (UINT leaderIndex = 0; leaderIndex < _leaders.Length(); leaderIndex++)
		{
			//for (UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++) buyRatio[supplyIndex] += allResults(playerIndex, leaderIndex).buyRatio[supplyIndex];
			file << '\t' << allResults(playerIndex, leaderIndex).winRatio[0];
		}

		//for (UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++) file << '\t' << buyRatio[supplyIndex];

		file << '\t' << _pool[playerIndex]->p->ControllerName() << endl;
	}

	return allResults;
}

void TestChamber::InitializeBuyPool(const CardDatabase &cards, PlayerType playerType)
{
    _pool.FreeMemory();
    for(UINT supplyIndexA = 0; supplyIndexA < 11; supplyIndexA++)
    {
        Card *a = NULL;
        if(supplyIndexA < 10) a = _gameOptions.supplyPiles[supplyIndexA];
        for(UINT supplyIndexB = 0; supplyIndexB < 11; supplyIndexB++)
        {
            Card *b = NULL;
            if(supplyIndexB < 10) b = _gameOptions.supplyPiles[supplyIndexB];

            //if(a != NULL && b != NULL) _pool.PushEnd(new TestPlayer(AIUtility::MakeRandomPlayer(cards, a, b)));
            //_pool.PushEnd(new TestPlayer(AIUtility::MakeTwoCardPlayer(cards, a, b)));


			TestPlayer *newPlayer;
			if (playerType == HEURISTIC_PLAYER)
				newPlayer = new TestPlayer(new PlayerHeuristic(new BuyAgendaMenu(cards, _gameOptions)));
			else if (playerType == STATEINFORMED_PLAYER){
				newPlayer = new TestPlayer(new PlayerStateInformed(new DecisionStrategy(cards, _gameOptions)));
			}
			else{ newPlayer = NULL; }

			if (_trainingType == TRAINING_DECISIONS) {
				newPlayer->p = newPlayer->p->MutateOnlyDecisions(cards, _gameOptions);
			}
			else if (_trainingType == TRAINING_BUYS){
				newPlayer->p = newPlayer->p->MutateOnlyBuys(cards, _gameOptions);
			}
			else {
				newPlayer->p = newPlayer->p->Mutate(cards, _gameOptions);
			}
            _pool.PushEnd(newPlayer);
            //newPlayer->p = new PlayerHeuristic(new BuyAgendaMenu(menu));
        }
    }

    for(UINT supplyIndexA = 0; supplyIndexA < 11; supplyIndexA++)
    {
        Card *a = NULL;
        if(supplyIndexA > 0) a = _gameOptions.supplyPiles[supplyIndexA - 1];
		if (supplyIndexA == 0){

			if (playerType == HEURISTIC_PLAYER)
				_bestLeaderHistory.PushEnd(new TestPlayer(new PlayerHeuristic(new BuyAgendaMenu(cards, _gameOptions, a, NULL))));
			else if (playerType == STATEINFORMED_PLAYER){
				_bestLeaderHistory.PushEnd(new TestPlayer(new PlayerStateInformed(new DecisionStrategy(cards, _gameOptions, a, NULL))));
			}
			else{}

		}
    }
    
    /*for(UINT mutationTestIndex = 0; mutationTestIndex < 1000000; mutationTestIndex++)
    {
    PlayerHeuristic *p = _pool.RandomElement()->p->Mutate(cards, options);
    for(UINT mutation = 0; mutation < 50; mutation++)
    {
    p = p->Mutate(cards, options);
    }
    }*/
}

const int DECISION_POOL_SIZE = 100;
void TestChamber::InitializeDecisionPool(const CardDatabase &cards, String buyMenu)
{
	_pool.FreeMemory();
	for (UINT numDecisionsInPool = 0; numDecisionsInPool < DECISION_POOL_SIZE; numDecisionsInPool++)
	{
		TestPlayer *newPlayer = new TestPlayer(new PlayerStateInformed(new DecisionStrategy(cards, buyMenu)));
		
		Console::WriteLine("new player:");
		Console::WriteLine(newPlayer->VisualizationDescription(_supplyCards, true));
		Console::WriteLine(newPlayer->VisualizationDescriptionDecisionStrategy());

		if (_trainingType == TRAINING_DECISIONS) {
			newPlayer->p = newPlayer->p->MutateOnlyDecisions(cards, _gameOptions);
		}
		else if (_trainingType == TRAINING_BUYS) {
			newPlayer->p = newPlayer->p->MutateOnlyBuys(cards, _gameOptions);
		}
		else {
			newPlayer->p = newPlayer->p->Mutate(cards, _gameOptions);
		}
		Console::WriteLine("np after mutate:");
		Console::WriteLine(newPlayer->VisualizationDescription(_supplyCards, true));
		Console::WriteLine(newPlayer->VisualizationDescriptionDecisionStrategy());
		_pool.PushEnd(newPlayer);
		//newPlayer->p = new PlayerHeuristic(new BuyAgendaMenu(menu));

	}
	/*for(UINT mutationTestIndex = 0; mutationTestIndex < 1000000; mutationTestIndex++)
	{
	PlayerHeuristic *p = _pool.RandomElement()->p->Mutate(cards, options);
	for(UINT mutation = 0; mutation < 50; mutation++)
	{
	p = p->Mutate(cards, options);
	}
	}*/
}


TestResult TestChamber::Test(const CardDatabase &cards, const TestParameters &params,bool useConsole)
{

#ifndef LOGGING_CONSTANT
    logging = false;
    decisionText = false;
#endif

    TestResult result;
    
    if(useConsole)
    {
        Console::WriteLine("*** AI Test, max " + String(params.maxGameCount) + " games");
        Console::WriteLine("Supply: " + params.options.ToString());
    }

    Vector<PlayerInfo> players;

    for(UINT playerIndex = 0; playerIndex < 2; playerIndex++)
    {
        result.winRatio[playerIndex] = 0.0;
        players.PushEnd(PlayerInfo(playerIndex, "p" + String(playerIndex), params.players[playerIndex]));
        if(useConsole) Console::WriteLine(players.Last().name + ": " + players.Last().controller->ControllerName());
    }

    DominionGame game;
    game.Init(cards);

    Clock timer;

    int gamesSampled = 0;
    bool significantDifferenceFound = false;
    for(UINT gameIndex = 0; gameIndex < params.maxGameCount && !significantDifferenceFound; gameIndex++)
    {
        game.NewGame(players, params.options);
        AIUtility::AdvanceAIs(game);
        gamesSampled++;

        const UINT supplyCount = game.data().supplyCards.Length();
        if(gameIndex == 0) result.buyRatio.Allocate(supplyCount, 0.0);
        for(UINT supplyIndex = 0; supplyIndex < supplyCount; supplyIndex++)
        {
            if(game.data().players[0].ledger.cardsBought.Contains(game.data().supplyCards[supplyIndex])) result.buyRatio[supplyIndex] += 1.0;
        }

        Vector<int> winners = game.state().WinningPlayers();
        for(UINT playerIndex = 0; playerIndex < 2; playerIndex++)
        {
            if(winners.Contains(playerIndex)) result.winRatio[playerIndex] += 1.0 / double(winners.Length());
        }

        if(gameIndex >= params.minGameCount && gamesSampled % 200 == 0)
        {
            double errorMargin = 2.0 / sqrt(gamesSampled);
            double spread = fabs((result.winRatio[0] - result.winRatio[1]) / double(gamesSampled));
            significantDifferenceFound = (errorMargin <= spread);
        }
    }

    for(UINT supplyIndex = 0; supplyIndex < result.buyRatio.Length(); supplyIndex++) result.buyRatio[supplyIndex] /= double(gamesSampled);

    for(UINT playerIndex = 0; playerIndex < 2; playerIndex++)
    {
        result.winRatio[playerIndex] /= double(gamesSampled);
        if(useConsole) Console::WriteLine(players[playerIndex].name + ": " + String(result.winRatio[playerIndex] * 100.0) + "%");
    }

    if(useConsole) Console::WriteLine("Time: " + String(timer.Elapsed()) + "s, games=" + String(gamesSampled));

    return result;
}


void TestChamber::SaveVisualizationFiles(const CardDatabase &cards)
{
	if (_generation % 4 == 0)
	{
		ComputeLeaderboard(cards, _leaders, _directory + "decision-leaderboard/" + String::ZeroPad(_generation, 3) + _metaSuffix + ".txt", _parameters.visualizationGameCount);
		_bestLeaderHistory.PushEnd(_leaders[0]);

		for (UINT leaderIndex = 0; leaderIndex < _leaders.Length(); leaderIndex++)
		{
			UINT curHash = _leaders[leaderIndex]->buyID.Hash32();
			if (_allLeaderHistoryHashes.Contains(curHash))
			{
				//
				// Override the previous version of this buy pattern, under thea assumption that the later generation one is better.
				//
				_allLeaderHistory[_allLeaderHistoryHashes.FindFirstIndex(curHash)] = _leaders[leaderIndex];
			}
			else
			{
				_allLeaderHistoryHashes.PushEnd(curHash);
				_allLeaderHistory.PushEnd(_leaders[leaderIndex]);
			}
		}

		ComputeProgression(cards, _leaders[0], _bestLeaderHistory, _directory + "decision-progression/" + String::ZeroPad(_generation, 3) + _metaSuffix + ".txt");

		if (_generation % 64 == 0)
		{
			//ComputeCounters(cards, directory + "counters/" + String::ZeroPad(_generation, 3) + "_" + String(_metaIndex) + ".txt");
		}
	}
}

void TestChamber::SaveVisualizationFilesDecisions(const CardDatabase &cards)
{
	if (_generation % 4 == 0)
	{
		ComputeLeaderboardDecisions(cards, _leaders, _directory + "decision-leaderboard/" + String::ZeroPad(_generation, 3) + _metaSuffix + ".txt", _parameters.visualizationGameCount);
		_bestLeaderHistory.PushEnd(_leaders[0]);

		for (UINT leaderIndex = 0; leaderIndex < _leaders.Length(); leaderIndex++)
		{
			const DecisionStrategy *strat = dynamic_cast<const DecisionStrategy*>(&_leaders[leaderIndex]->p->Agenda());
			UINT curHash = strat->getStringInfo().Hash32();
			if (_allLeaderHistoryHashes.Contains(curHash))
			{
				//
				// Override the previous version of this buy pattern, under thea assumption that the later generation one is better.
				//
				_allLeaderHistory[_allLeaderHistoryHashes.FindFirstIndex(curHash)] = _leaders[leaderIndex];
			}
			else
			{
				_allLeaderHistoryHashes.PushEnd(curHash);
				_allLeaderHistory.PushEnd(_leaders[leaderIndex]);
			}
		}

		ComputeProgressionDecisions(cards, _leaders[0], _bestLeaderHistory, _directory + "decisions-progression/" + String::ZeroPad(_generation, 3) + _metaSuffix + ".txt");

		if (_generation % 64 == 0)
		{
			//ComputeCounters(cards, directory + "counters/" + String::ZeroPad(_generation, 3) + "_" + String(_metaIndex) + ".txt");
		}
	}
}



//////

void TestChamber::StrategizeStartBuys(const CardDatabase &cards, const GameOptions &options, const String &directory, const String &metaSuffix, PlayerType playerType, String buyMenu)
{
	FreeMemory();
	_gameOptions = options;
	_metaSuffix = metaSuffix;
	_directory = directory;
	_trainingType = TRAINING_BUYS;
	_playerType = playerType;

	//
	// Higher-ranked leaders receive higher weight to reward strategies that are able to defeat them
	//
	_leaderWeights.Allocate(_parameters.leaderCount);
	for (UINT leaderIndex = 0; leaderIndex < _parameters.leaderCount; leaderIndex++)
	{
		_leaderWeights[leaderIndex] = Math::LinearMap(0.0, _parameters.leaderCount - 1.0, _parameters.leaderCurveMin, _parameters.leaderCurveMax, double(leaderIndex));
	}
	double sum = _leaderWeights.Sum();
	for (UINT leaderIndex = 0; leaderIndex < _parameters.leaderCount; leaderIndex++) _leaderWeights[leaderIndex] /= sum;

	if (_parameters.leaderCount == 1) _leaderWeights[0] = 1.0;

	//
	// Make a game just so we can easily grab the supply piles
	//
	DominionGame g;
	g.Init(cards);
	Vector<PlayerInfo> playerList;
	playerList.PushEnd(PlayerInfo(0, "p0", new PlayerHuman()));
	playerList.PushEnd(PlayerInfo(1, "p1", new PlayerHuman()));
	g.NewGame(playerList, _gameOptions);
	_supplyCards = g.data().supplyCards;

	
	//
	// Start the pool and leaders with variations on big money
	//
	InitializeBuyPool(cards, playerType);
	

	while (_leaders.Length() < _parameters.leaderCount)
	{
		_leaders.PushEnd(_pool.RandomElement());
	}

	Console::WriteLine("strategize started using player type:" + String(playerType));
}


void TestChamber::StrategizeStepBuys(const CardDatabase &cards, PlayerType playerType)
{
	Assert(_pool.Length() != 0, "StrategizeStart not called");

	Console::WriteLine("Testing generation " + String(_generation) + _metaSuffix + " using player type:" + String(playerType));

	//
	// Run all AIs in the current pool against the leaders
	//
	Grid<TestResult> poolResults = TestBuyPool(cards, _directory + "generations/" + String::ZeroPad(_generation, 3) + _metaSuffix + ".txt");

	AssignBuyIDs(poolResults);

	//
	// Sort the pool according to their scores relative to the previous leaders
	//
	_pool.Sort([](const TestPlayer *a, const TestPlayer *b) { return a->rating > b->rating; });

	Console::WriteLine("generation" + String::ZeroPad(_generation, 3) + ", leader win percentage: " + String(_pool[0]->rating * 100.0) + "%" + " using player type:" + String(playerType));
	Console::WriteLine(_pool[0]->VisualizationDescription(_supplyCards, true));

	AssignNewLeaders(cards, playerType);

	GenerateNewPool(cards, playerType);

	SaveVisualizationFiles(cards);

	_generation++;
}


void TestChamber::StrategizeStartDecisions(const CardDatabase &cards, const GameOptions &options, const String &directory, const String &metaSuffix, PlayerType playerType, String buyMenu)
{
	FreeMemory();
	_gameOptions = options;
	_metaSuffix = metaSuffix;
	_directory = directory;

	_trainingType = TRAINING_DECISIONS;
	_playerType = playerType;

	//
	// Higher-ranked leaders receive higher weight to reward strategies that are able to defeat them
	//
	_leaderWeights.Allocate(_parameters.leaderCount);
	for (UINT leaderIndex = 0; leaderIndex < _parameters.leaderCount; leaderIndex++)
	{
		_leaderWeights[leaderIndex] = Math::LinearMap(0.0, _parameters.leaderCount - 1.0, _parameters.leaderCurveMin, _parameters.leaderCurveMax, double(leaderIndex));
	}
	double sum = _leaderWeights.Sum();
	for (UINT leaderIndex = 0; leaderIndex < _parameters.leaderCount; leaderIndex++) _leaderWeights[leaderIndex] /= sum;

	if (_parameters.leaderCount == 1) _leaderWeights[0] = 1.0;

	//
	// Make a game just so we can easily grab the supply piles
	//
	DominionGame g;
	g.Init(cards);
	Vector<PlayerInfo> playerList;
	playerList.PushEnd(PlayerInfo(0, "p0", new PlayerHuman()));
	playerList.PushEnd(PlayerInfo(1, "p1", new PlayerHuman()));
	g.NewGame(playerList, _gameOptions);
	_supplyCards = g.data().supplyCards;

	
	Console::WriteLine(buyMenu);
	// Start the pool with same buyDecisions and random weights

	//Should have leaderboards already 
	//load from dir

	//directory

	InitializeDecisionPool(cards, buyMenu);
	

	while (_leaders.Length() < _parameters.leaderCount)
	{
		_leaders.PushEnd(_pool.RandomElement());
	}

	Console::WriteLine("strategize started using player type:" + String(playerType));
}


void TestChamber::StrategizeStepDecisions(const CardDatabase &cards, PlayerType playerType)
{
	Assert(_pool.Length() != 0, "StrategizeStart not called");

	Console::WriteLine("Testing generation " + String(_generation) + _metaSuffix + " using player type:" + String(playerType));

	//
	// Run all AIs in the current pool against the leaders
	//
	Grid<TestResult> poolResults = TestDecisionPool(cards, _directory + "decision-generations/" + String::ZeroPad(_generation, 3) + _metaSuffix + ".txt");

	AssignBuyIDs(poolResults);

	//
	// Sort the pool according to their scores relative to the previous leaders
	//
	_pool.Sort([](const TestPlayer *a, const TestPlayer *b) { return a->rating > b->rating; });

	Console::WriteLine("generation" + String::ZeroPad(_generation, 3) + ", leader win percentage: " + String(_pool[0]->rating * 100.0) + "%" + " using player type:" + String(playerType));
	Console::WriteLine(_pool[0]->VisualizationDescription(_supplyCards, true));
	Console::WriteLine(_pool[0]->VisualizationDescriptionDecisionStrategy());

	AssignNewLeaders(cards, playerType);

	GenerateNewPool(cards, playerType);

	SaveVisualizationFilesDecisions(cards);

	_generation++;
}
