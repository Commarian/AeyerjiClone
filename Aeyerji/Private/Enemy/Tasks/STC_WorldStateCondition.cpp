#include "Enemy/Tasks/STC_WorldStateCondition.h"

#include "Systems/AeyerjiWorldStateSubsystem.h"
#include "StateTreeExecutionContext.h"

bool USTC_WorldStateCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const UObject* Owner = Context.GetOwner();
	const UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(Owner);
	if (!WorldStateSubsystem)
	{
		return bInvert;
	}

	FAeyerjiWorldStateEntry Entry;
	const bool bFound = WorldStateSubsystem->GetEntry(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), Entry);

	bool bResult = false;
	if (CompareOp == EAeyerjiWorldStateCompareOp::Exists)
	{
		bResult = bFound;
	}
	else if (CompareOp == EAeyerjiWorldStateCompareOp::DoesNotExist)
	{
		bResult = !bFound;
	}
	else if (bFound)
	{
		bResult = CompareValues(Entry.Value);
	}

	return bInvert ? !bResult : bResult;
}

bool USTC_WorldStateCondition::CompareValues(const FAeyerjiWorldStateValue& ActualValue) const
{
	switch (CompareOp)
	{
	case EAeyerjiWorldStateCompareOp::Equals:
		return ActualValue.Equals(ExpectedValue);
	case EAeyerjiWorldStateCompareOp::NotEquals:
		return !ActualValue.Equals(ExpectedValue);
	case EAeyerjiWorldStateCompareOp::Greater:
	case EAeyerjiWorldStateCompareOp::GreaterOrEqual:
	case EAeyerjiWorldStateCompareOp::Less:
	case EAeyerjiWorldStateCompareOp::LessOrEqual:
	{
		double ActualNumber = 0.0;
		double ExpectedNumber = 0.0;
		if (!ActualValue.TryGetNumericValue(ActualNumber) || !ExpectedValue.TryGetNumericValue(ExpectedNumber))
		{
			return false;
		}

		if (CompareOp == EAeyerjiWorldStateCompareOp::Greater)
		{
			return ActualNumber > ExpectedNumber;
		}
		if (CompareOp == EAeyerjiWorldStateCompareOp::GreaterOrEqual)
		{
			return ActualNumber >= ExpectedNumber;
		}
		if (CompareOp == EAeyerjiWorldStateCompareOp::Less)
		{
			return ActualNumber < ExpectedNumber;
		}
		return ActualNumber <= ExpectedNumber;
	}
	default:
		return false;
	}
}
