// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SequencerPrivatePCH.h"
#include "Sequencer.h"
#include "SequencerDisplayNode.h"
#include "SequencerEntityVisitor.h"
#include "MovieSceneSection.h"
#include "ISequencerSection.h"

FSequencerEntityRange::FSequencerEntityRange(const TRange<float>& InRange)
	: StartTime(InRange.GetLowerBoundValue()), EndTime(InRange.GetUpperBoundValue())
{
}

FSequencerEntityRange::FSequencerEntityRange(FVector2D TopLeft, FVector2D BottomRight)
	: StartTime(TopLeft.X), EndTime(BottomRight.X)
	, VerticalTop(TopLeft.Y), VerticalBottom(BottomRight.Y)
{
}

bool FSequencerEntityRange::IntersectSection(const UMovieSceneSection* InSection) const
{
	return InSection->GetStartTime() <= EndTime && InSection->GetEndTime() >= StartTime;
}

bool FSequencerEntityRange::IntersectNode(const FSequencerDisplayNode& InNode) const
{
	if (VerticalTop.IsSet())
	{
		return InNode.GetVirtualTop() <= VerticalBottom.GetValue() && InNode.GetVirtualBottom() >= VerticalTop.GetValue();
	}
	return true;
}

bool FSequencerEntityRange::IntersectKeyArea(const FSequencerDisplayNode& InNode, float VirtualKeyHeight) const
{
	if (VerticalTop.IsSet())
	{
		const float NodeCenter = InNode.GetVirtualTop() + (InNode.GetVirtualBottom() - InNode.GetVirtualTop())/2;
		return NodeCenter + VirtualKeyHeight/2 > VerticalTop.GetValue() && NodeCenter - VirtualKeyHeight/2 < VerticalBottom.GetValue();
	}
	return true;
}

FSequencerEntityWalker::FSequencerEntityWalker(const FSequencerEntityRange& InRange, FVector2D InVirtualKeySize)
	: Range(InRange), VirtualKeySize(InVirtualKeySize)
{}

/* @todo: Could probably optimize this by not walking every single node, and binary searching the begin<->end ranges instead */
void FSequencerEntityWalker::Traverse(const ISequencerEntityVisitor& Visitor, const TArray< TSharedRef<FSequencerDisplayNode> >& Nodes)
{
	for (auto& Child : Nodes)
	{
		if (!Child->IsHidden())
		{
			HandleNode(Visitor, *Child);
		}
	}
}

void FSequencerEntityWalker::HandleNode(const ISequencerEntityVisitor& Visitor, FSequencerDisplayNode& InNode)
{
	if (InNode.GetType() == ESequencerNode::Track)
	{
		HandleNode(Visitor, InNode, static_cast<FTrackNode&>(InNode).GetSections());
	}

	if (InNode.IsExpanded())
	{
		for (auto& Child : InNode.GetChildNodes())
		{
			if (!Child->IsHidden())
			{
				HandleNode(Visitor, *Child);
			}
		}
	}
}

void FSequencerEntityWalker::HandleNode(const ISequencerEntityVisitor& Visitor, FSequencerDisplayNode& InNode, const TArray<TSharedRef<ISequencerSection>>& InSections)
{
	if (Range.IntersectNode(InNode))
	{
		bool bNodeHasKeyArea = false;
		if (InNode.GetType() == ESequencerNode::KeyArea)
		{
			HandleKeyAreaNode(Visitor, static_cast<FSectionKeyAreaNode&>(InNode), InSections);
			bNodeHasKeyArea = true;
		}
		else if (InNode.GetType() == ESequencerNode::Track)
		{
			TSharedPtr<FSectionKeyAreaNode> SectionKeyNode = static_cast<FTrackNode&>(InNode).GetTopLevelKeyNode();
			if (SectionKeyNode.IsValid())
			{
				HandleKeyAreaNode(Visitor, static_cast<FSectionKeyAreaNode&>(InNode), InSections);
				bNodeHasKeyArea = true;
			}
		}

		if (!bNodeHasKeyArea && (!InNode.IsExpanded() || InNode.GetChildNodes().Num() == 0))
		{
			// As a fallback, we need to handle:
			//  - Key groupings on collapsed parents
			//  - Sections that have no key areas
			for (int32 SectionIndex = 0; SectionIndex < InSections.Num(); ++SectionIndex)
			{
				UMovieSceneSection* Section = InSections[SectionIndex]->GetSectionObject();
				if (Range.IntersectSection(Section))
				{
					Visitor.VisitSection(Section);

					// Only handle grouped keys if we actually have children
					if (InNode.GetChildNodes().Num() != 0 && Range.IntersectKeyArea(InNode, VirtualKeySize.X))
					{
						TSharedRef<IKeyArea> KeyArea = InNode.UpdateKeyGrouping(SectionIndex);
						HandleKeyArea(Visitor, KeyArea, Section);
					}
				}
			}
		}
	}

	if (InNode.IsExpanded())
	{
		// Handle Children
		for (auto& Child : InNode.GetChildNodes())
		{
			if (!Child->IsHidden())
			{
				HandleNode(Visitor, *Child, InSections);
			}
		}
	}
}

void FSequencerEntityWalker::HandleKeyAreaNode(const ISequencerEntityVisitor& Visitor, FSectionKeyAreaNode& KeyAreaNode, const TArray<TSharedRef<ISequencerSection>>& InSections)
{
	for( int32 SectionIndex = 0; SectionIndex < InSections.Num(); ++SectionIndex )
	{
		UMovieSceneSection* Section = InSections[SectionIndex]->GetSectionObject();

		// If the section is at all within the marquee, we check its keys
		if (Range.IntersectSection(Section))
		{
			Visitor.VisitSection(Section);

			if (Range.IntersectKeyArea(KeyAreaNode, VirtualKeySize.X))
			{
				TSharedRef<IKeyArea> KeyArea = KeyAreaNode.GetKeyArea(SectionIndex);
				HandleKeyArea(Visitor, KeyArea, InSections[SectionIndex]->GetSectionObject());
			}
		}
	}
}

void FSequencerEntityWalker::HandleKeyArea(const ISequencerEntityVisitor& Visitor, const TSharedPtr<IKeyArea>& KeyArea, UMovieSceneSection* Section)
{
	for (FKeyHandle KeyHandle : KeyArea->GetUnsortedKeyHandles())
	{
		float KeyPosition = KeyArea->GetKeyTime(KeyHandle);
		if (KeyPosition + VirtualKeySize.X/2 > Range.StartTime &&
			KeyPosition - VirtualKeySize.X/2 < Range.EndTime)
		{
			Visitor.VisitKey(KeyHandle, KeyPosition, KeyArea, Section);
		}
	}
}