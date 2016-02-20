// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "SequencerPrivatePCH.h"
#include "SequencerObjectBindingNode.h"
#include "Sequencer.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "MovieSceneTrack.h"
#include "ObjectEditorUtils.h"
#include "AssetEditorManager.h"
#include "SequencerUtilities.h"
#include "ClassIconFinder.h"
#include "SequencerCommands.h"

#define LOCTEXT_NAMESPACE "FObjectBindingNode"


namespace SequencerNodeConstants
{
	extern const float CommonPadding;
}


void GetKeyablePropertyPaths(UClass* Class, UStruct* PropertySource, TArray<UProperty*>& PropertyPath, FSequencer& Sequencer, TArray<TArray<UProperty*>>& KeyablePropertyPaths)
{
	//@todo need to resolve this between UMG and the level editor sequencer
	const bool bRecurseAllProperties = Sequencer.IsLevelEditorSequencer();

	for (TFieldIterator<UProperty> PropertyIterator(PropertySource); PropertyIterator; ++PropertyIterator)
	{
		UProperty* Property = *PropertyIterator;

		if (Property && !Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			PropertyPath.Add(Property);

			bool bIsPropertyKeyable = Sequencer.CanKeyProperty(FCanKeyPropertyParams(Class, PropertyPath));
			if (bIsPropertyKeyable)
			{
				KeyablePropertyPaths.Add(PropertyPath);
			}

			if (!bIsPropertyKeyable || bRecurseAllProperties)
			{
				UStructProperty* StructProperty = Cast<UStructProperty>(Property);
				if (StructProperty != nullptr)
				{
					GetKeyablePropertyPaths(Class, StructProperty->Struct, PropertyPath, Sequencer, KeyablePropertyPaths);
				}
			}

			PropertyPath.RemoveAt(PropertyPath.Num() - 1);
		}
	}
}


struct PropertyMenuData
{
	FString MenuName;
	TArray<UProperty*> PropertyPath;
};



FSequencerObjectBindingNode::FSequencerObjectBindingNode(FName NodeName, const FText& InDisplayName, const FGuid& InObjectBinding, TSharedPtr<FSequencerDisplayNode> InParentNode, FSequencerNodeTree& InParentTree)
	: FSequencerDisplayNode(NodeName, InParentNode, InParentTree)
	, ObjectBinding(InObjectBinding)
	, DefaultDisplayName(InDisplayName)
{
	UMovieScene* MovieScene = GetSequencer().GetFocusedMovieSceneSequence()->GetMovieScene();

	if (MovieScene->FindPossessable(ObjectBinding))
	{
		BindingType = EObjectBindingType::Possessable;
	}
	else if (MovieScene->FindSpawnable(ObjectBinding))
	{
		BindingType = EObjectBindingType::Spawnable;
	}
	else
	{
		BindingType = EObjectBindingType::Unknown;
	}
}

/* FSequencerDisplayNode interface
 *****************************************************************************/

void FSequencerObjectBindingNode::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	if (GetSequencer().IsLevelEditorSequencer())
	{
		UMovieScene* MovieScene = GetSequencer().GetFocusedMovieSceneSequence()->GetMovieScene();
		FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(ObjectBinding);

		if (Spawnable)
		{
			UBlueprint* Blueprint = Cast<UBlueprint>(Spawnable->GetClass()->ClassGeneratedBy);

			if (Blueprint)
			{
				FFormatNamedArguments Args;
				MenuBuilder.AddMenuEntry(
					FText::Format( LOCTEXT("EditDefaults", "Edit Defaults"), Args),
					FText::Format( LOCTEXT("EditDefaultsTooltip", "Edit the defaults for this spawnable object"), Args ),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([=]{
						FAssetEditorManager::Get().OpenEditorForAsset(Blueprint);
					}))
				);
			}

			MenuBuilder.AddSubMenu(
				LOCTEXT("OwnerLabel", "Spawned Object Owner"),
				LOCTEXT("OwnerTooltip", "Specifies how the spawned object is to be owned"),
				FNewMenuDelegate::CreateSP(this, &FSequencerObjectBindingNode::AddSpawnOwnershipMenu)
			);

		}
		else
		{
			const UClass* ObjectClass = GetClassForObjectBinding();
			
			if (ObjectClass->IsChildOf(AActor::StaticClass()))
			{
				FFormatNamedArguments Args;

				MenuBuilder.AddSubMenu(
					FText::Format( LOCTEXT("Assign Actor ", "Assign Actor"), Args),
					FText::Format( LOCTEXT("AssignActorTooltip", "Assign an actor to this track"), Args ),
					FNewMenuDelegate::CreateRaw(&GetSequencer(), &FSequencer::AssignActor, ObjectBinding));
			}

			MenuBuilder.AddMenuEntry( FSequencerCommands::Get().ConvertToSpawnable );
		}

		MenuBuilder.BeginSection("Organize", LOCTEXT("OrganizeContextMenuSectionName", "Organize"));
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("LabelsSubMenuText", "Labels"),
				LOCTEXT("LabelsSubMenuTip", "Add or remove labels on this track"),
				FNewMenuDelegate::CreateSP(this, &FSequencerObjectBindingNode::HandleLabelsSubMenuCreate)
			);
		}
		MenuBuilder.EndSection();
	}

	FSequencerDisplayNode::BuildContextMenu(MenuBuilder);
}

void FSequencerObjectBindingNode::AddSpawnOwnershipMenu(FMenuBuilder& MenuBuilder)
{
	FMovieSceneSpawnable* Spawnable = GetSequencer().GetFocusedMovieSceneSequence()->GetMovieScene()->FindSpawnable(ObjectBinding);
	if (!Spawnable)
	{
		return;
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ThisSequence_Label", "This Sequence"),
		LOCTEXT("ThisSequence_Tooltip", "Indicates that this sequence will own the spawned object. The object will be destroyed at the end of the sequence."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=]{ Spawnable->SetSpawnOwnership(ESpawnOwnership::InnerSequence); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([=]{ return Spawnable->GetSpawnOwnership() == ESpawnOwnership::InnerSequence; })
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MasterSequence_Label", "Master Sequence"),
		LOCTEXT("MasterSequence_Tooltip", "Indicates that the outermost sequence will own the spawned object. The object will be destroyed when the outermost sequence stops playing."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=]{ Spawnable->SetSpawnOwnership(ESpawnOwnership::MasterSequence); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([=]{ return Spawnable->GetSpawnOwnership() == ESpawnOwnership::MasterSequence; })
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("External_Label", "External"),
		LOCTEXT("External_Tooltip", "Indicates this object's lifetime is managed externally once spawned. It will not be destroyed by sequencer."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=]{ Spawnable->SetSpawnOwnership(ESpawnOwnership::External); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([=]{ return Spawnable->GetSpawnOwnership() == ESpawnOwnership::External; })
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);
}

bool FSequencerObjectBindingNode::CanRenameNode() const
{
	return true;
}

TSharedRef<SWidget> FSequencerObjectBindingNode::GetCustomOutlinerContent()
{
	// Create a container edit box
	TSharedRef<SHorizontalBox> BoxPanel = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(SSpacer)
		];


	TAttribute<bool> HoverState = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &FSequencerDisplayNode::IsHovered));

	BoxPanel->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		FSequencerUtilities::MakeAddButton(LOCTEXT("TrackText", "Track"), FOnGetContent::CreateSP(this, &FSequencerObjectBindingNode::HandleAddTrackComboButtonGetMenuContent), HoverState)
	];

	const UClass* ObjectClass = GetClassForObjectBinding();
	GetSequencer().BuildObjectBindingEditButtons(BoxPanel, ObjectBinding, ObjectClass);

	return BoxPanel;
}


FText FSequencerObjectBindingNode::GetDisplayName() const
{
	UMovieScene* MovieScene = GetSequencer().GetFocusedMovieSceneSequence()->GetMovieScene();

	if (MovieScene != nullptr)
	{
		return MovieScene->GetObjectDisplayName(ObjectBinding);
	}

	return DefaultDisplayName;
}

const FSlateBrush* FSequencerObjectBindingNode::GetIconBrush() const
{
	return FClassIconFinder::FindIconForClass(GetClassForObjectBinding());
}

const FSlateBrush* FSequencerObjectBindingNode::GetIconOverlayBrush() const
{
	if (BindingType == EObjectBindingType::Spawnable)
	{
		return FEditorStyle::GetBrush("Sequencer.SpawnableIconOverlay");
	}
	return nullptr;
}

FText FSequencerObjectBindingNode::GetIconToolTipText() const
{
	if (BindingType == EObjectBindingType::Spawnable)
	{
		return LOCTEXT("SpawnableToolTip", "This item is spawned by sequencer according to this object's spawn track.");
	}
	else if (BindingType == EObjectBindingType::Possessable)
	{
		return LOCTEXT("PossessableToolTip", "This item is a possessable reference to an existing object.");
	}

	return FText();
}

float FSequencerObjectBindingNode::GetNodeHeight() const
{
	return SequencerLayoutConstants::ObjectNodeHeight + SequencerNodeConstants::CommonPadding*2;
}


FNodePadding FSequencerObjectBindingNode::GetNodePadding() const
{
	return FNodePadding(0.f);//SequencerNodeConstants::CommonPadding);
}


ESequencerNode::Type FSequencerObjectBindingNode::GetType() const
{
	return ESequencerNode::Object;
}


void FSequencerObjectBindingNode::SetDisplayName(const FText& NewDisplayName)
{
	UMovieScene* MovieScene = GetSequencer().GetFocusedMovieSceneSequence()->GetMovieScene();

	if (MovieScene != nullptr)
	{
		MovieScene->SetObjectDisplayName(ObjectBinding, NewDisplayName);
	}
}


/* FSequencerObjectBindingNode implementation
 *****************************************************************************/

void FSequencerObjectBindingNode::AddPropertyMenuItems(FMenuBuilder& AddTrackMenuBuilder, TArray<TArray<UProperty*> > KeyableProperties, int32 PropertyNameIndexStart, int32 PropertyNameIndexEnd)
{
	TArray<PropertyMenuData> KeyablePropertyMenuData;

	for (auto KeyableProperty : KeyableProperties)
	{
		TArray<FString> PropertyNames;
		if (PropertyNameIndexEnd == -1)
		{
			PropertyNameIndexEnd = KeyableProperty.Num();
		}

		//@todo
		if (PropertyNameIndexStart >= KeyableProperty.Num())
		{
			continue;
		}

		for (int32 PropertyNameIndex = PropertyNameIndexStart; PropertyNameIndex < PropertyNameIndexEnd; ++PropertyNameIndex)
		{
			PropertyNames.Add(KeyableProperty[PropertyNameIndex]->GetDisplayNameText().ToString());
		}

		PropertyMenuData KeyableMenuData;
		{
			KeyableMenuData.PropertyPath = KeyableProperty;
			KeyableMenuData.MenuName = FString::Join( PropertyNames, TEXT( "." ) );
		}

		KeyablePropertyMenuData.Add(KeyableMenuData);
	}

	// Sort on the menu name
	KeyablePropertyMenuData.Sort([](const PropertyMenuData& A, const PropertyMenuData& B)
	{
		int32 CompareResult = A.MenuName.Compare(B.MenuName);
		return CompareResult < 0;
	});

	// Add menu items
	for (int32 MenuDataIndex = 0; MenuDataIndex < KeyablePropertyMenuData.Num(); ++MenuDataIndex)
	{
		FUIAction AddTrackMenuAction(FExecuteAction::CreateSP(this, &FSequencerObjectBindingNode::HandlePropertyMenuItemExecute, KeyablePropertyMenuData[MenuDataIndex].PropertyPath));
		AddTrackMenuBuilder.AddMenuEntry(FText::FromString(KeyablePropertyMenuData[MenuDataIndex].MenuName), FText(), FSlateIcon(), AddTrackMenuAction);
	}
}


const UClass* FSequencerObjectBindingNode::GetClassForObjectBinding() const
{
	UMovieScene* MovieScene = GetSequencer().GetFocusedMovieSceneSequence()->GetMovieScene();
	FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(ObjectBinding);
	FMovieScenePossessable* Possessable = MovieScene->FindPossessable(ObjectBinding);
	
	// should exist, but also shouldn't be both a spawnable and a possessable
	check((Spawnable != nullptr) ^ (Possessable != nullptr));
	check((nullptr == Spawnable) || (nullptr != Spawnable->GetClass())  );
	const UClass* ObjectClass = Spawnable ? Spawnable->GetClass()->GetSuperClass() : Possessable->GetPossessedObjectClass();

	return ObjectClass;
}

UObject* FSequencerObjectBindingNode::FindRepresentativeObject()
{
	FSequencer& Sequencer = GetSequencer();
	
	UObject* BoundObject = Sequencer.GetFocusedMovieSceneSequenceInstance()->FindObject(ObjectBinding, Sequencer);
	if (BoundObject)
	{
		return BoundObject;
	}

	UMovieScene* FocusedMovieScene = Sequencer.GetFocusedMovieSceneSequence()->GetMovieScene();

	FMovieScenePossessable* Possessable = FocusedMovieScene->FindPossessable(ObjectBinding);
	// If we're a possessable with a parent spawnable and we don't have the object, we look the object up within the default object of the spawnable
	if (Possessable && Possessable->GetParent().IsValid())
	{
		// If we're a spawnable and we don't have the object, use the default object to build up the track menu
		FMovieSceneSpawnable* ParentSpawnable = FocusedMovieScene->FindSpawnable(Possessable->GetParent());
		if (ParentSpawnable)
		{
			UObject* ParentObject = ParentSpawnable->GetClass()->GetDefaultObject();
			if (ParentObject)
			{
				return Sequencer.GetFocusedMovieSceneSequence()->FindPossessableObject(ObjectBinding, ParentObject);
			}
		}
	}
	// If we're a spawnable and we don't have the object, use the default object to build up the track menu
	else if (FMovieSceneSpawnable* Spawnable = FocusedMovieScene->FindSpawnable(ObjectBinding))
	{
		return Spawnable->GetClass()->GetDefaultObject();
	}

	return nullptr;
}

/* FSequencerObjectBindingNode callbacks
 *****************************************************************************/

TSharedRef<SWidget> FSequencerObjectBindingNode::HandleAddTrackComboButtonGetMenuContent()
{
	FSequencer& Sequencer = GetSequencer();

	//@todo need to resolve this between UMG and the level editor sequencer
	const bool bUseSubMenus = Sequencer.IsLevelEditorSequencer();

	UObject* BoundObject = FindRepresentativeObject();

	ISequencerModule& SequencerModule = FModuleManager::GetModuleChecked<ISequencerModule>( "Sequencer" );
	TSharedRef<FUICommandList> CommandList(new FUICommandList);
	FMenuBuilder AddTrackMenuBuilder(true, nullptr, SequencerModule.GetMenuExtensibilityManager()->GetAllExtenders(CommandList, TArrayBuilder<UObject*>().Add(BoundObject)));

	const UClass* ObjectClass = GetClassForObjectBinding();
	AddTrackMenuBuilder.BeginSection(NAME_None, LOCTEXT("TracksMenuHeader" , "Tracks"));
	GetSequencer().BuildObjectBindingTrackMenu(AddTrackMenuBuilder, ObjectBinding, ObjectClass);
	AddTrackMenuBuilder.EndSection();

	TArray<TArray<UProperty*>> KeyablePropertyPaths;

	if (BoundObject != nullptr)
	{
		TArray<UProperty*> PropertyPath;
		GetKeyablePropertyPaths(BoundObject->GetClass(), BoundObject->GetClass(), PropertyPath, Sequencer, KeyablePropertyPaths);
	}

	// [Aspect Ratio]
	// [PostProcess Settings] [Bloom1Tint] [X]
	// [PostProcess Settings] [Bloom1Tint] [Y]
	// [PostProcess Settings] [ColorGrading]
	// [Ortho View]

	// Create property menu data based on keyable property paths
	TArray<PropertyMenuData> KeyablePropertyMenuData;
	for (auto KeyablePropertyPath : KeyablePropertyPaths)
	{
		PropertyMenuData KeyableMenuData;
		KeyableMenuData.PropertyPath = KeyablePropertyPath;
		KeyableMenuData.MenuName = KeyablePropertyPath[0]->GetDisplayNameText().ToString();
		KeyablePropertyMenuData.Add(KeyableMenuData);
	}

	// Sort on the menu name
	KeyablePropertyMenuData.Sort([](const PropertyMenuData& A, const PropertyMenuData& B)
	{
		int32 CompareResult = A.MenuName.Compare(B.MenuName);
		return CompareResult < 0;
	});
	

	// Add menu items
	AddTrackMenuBuilder.BeginSection( SequencerMenuExtensionPoints::AddTrackMenu_PropertiesSection, LOCTEXT("PropertiesMenuHeader" , "Properties"));
	for (int32 MenuDataIndex = 0; MenuDataIndex < KeyablePropertyMenuData.Num(); )
	{
		TArray<TArray<UProperty*> > KeyableSubMenuPropertyPaths;

		KeyableSubMenuPropertyPaths.Add(KeyablePropertyMenuData[MenuDataIndex].PropertyPath);

		// If this menu data only has one property name, add the menu item
		if (KeyablePropertyMenuData[MenuDataIndex].PropertyPath.Num() == 1 || !bUseSubMenus)
		{
			AddPropertyMenuItems(AddTrackMenuBuilder, KeyableSubMenuPropertyPaths);
			++MenuDataIndex;
		}
		// Otherwise, look to the next menu data to gather up new data
		else
		{
			for (; MenuDataIndex < KeyablePropertyMenuData.Num()-1; )
			{
				if (KeyablePropertyMenuData[MenuDataIndex].MenuName == KeyablePropertyMenuData[MenuDataIndex+1].MenuName)
				{	
					++MenuDataIndex;
					KeyableSubMenuPropertyPaths.Add(KeyablePropertyMenuData[MenuDataIndex].PropertyPath);
				}
				else
				{
					break;
				}
			}

			AddTrackMenuBuilder.AddSubMenu(
				FText::FromString(KeyablePropertyMenuData[MenuDataIndex].MenuName),
				FText::GetEmpty(), 
				FNewMenuDelegate::CreateSP(this, &FSequencerObjectBindingNode::HandleAddTrackSubMenuNew, KeyableSubMenuPropertyPaths));

			++MenuDataIndex;
		}
	}
	AddTrackMenuBuilder.EndSection();

	return AddTrackMenuBuilder.MakeWidget();
}


void FSequencerObjectBindingNode::HandleAddTrackSubMenuNew(FMenuBuilder& AddTrackMenuBuilder, TArray<TArray<UProperty*> > KeyablePropertyPaths)
{
	// [PostProcessSettings] [Bloom1Tint] [X]
	// [PostProcessSettings] [Bloom1Tint] [Y]
	// [PostProcessSettings] [ColorGrading]

	// Create property menu data based on keyable property paths
	TSet<UProperty*> PropertiesTraversed;
	TArray<PropertyMenuData> KeyablePropertyMenuData;
	for (auto KeyablePropertyPath : KeyablePropertyPaths)
	{		
		PropertyMenuData KeyableMenuData;
		KeyableMenuData.PropertyPath = KeyablePropertyPath;

		// If the path is greater than 1, keep track of the actual properties (not channels) and only add these properties once since we can't do single channel keying of a property yet.
		if (KeyablePropertyPath.Num() > 1) //@todo
		{
			if (PropertiesTraversed.Find(KeyablePropertyPath[1]) != nullptr)
			{
				continue;
			}

			KeyableMenuData.MenuName = FObjectEditorUtils::GetCategoryFName(KeyablePropertyPath[1]).ToString();
			PropertiesTraversed.Add(KeyablePropertyPath[1]);
		}
		else
		{
			// No sub menus items, so skip
			continue; 
		}
		KeyablePropertyMenuData.Add(KeyableMenuData);
	}

	// Sort on the menu name
	KeyablePropertyMenuData.Sort([](const PropertyMenuData& A, const PropertyMenuData& B)
	{
		int32 CompareResult = A.MenuName.Compare(B.MenuName);
		return CompareResult < 0;
	});

	// Add menu items
	for (int32 MenuDataIndex = 0; MenuDataIndex < KeyablePropertyMenuData.Num(); )
	{
		TArray<TArray<UProperty*> > KeyableSubMenuPropertyPaths;
		KeyableSubMenuPropertyPaths.Add(KeyablePropertyMenuData[MenuDataIndex].PropertyPath);

		for (; MenuDataIndex < KeyablePropertyMenuData.Num()-1; )
		{
			if (KeyablePropertyMenuData[MenuDataIndex].MenuName == KeyablePropertyMenuData[MenuDataIndex+1].MenuName)
			{
				++MenuDataIndex;
				KeyableSubMenuPropertyPaths.Add(KeyablePropertyMenuData[MenuDataIndex].PropertyPath);
			}
			else
			{
				break;
			}
		}

		const int32 PropertyNameIndexStart = 1; // Strip off the struct property name
		const int32 PropertyNameIndexEnd = 2; // Stop at the property name, don't descend into the channels

		AddTrackMenuBuilder.AddSubMenu(
			FText::FromString(KeyablePropertyMenuData[MenuDataIndex].MenuName),
			FText::GetEmpty(), 
			FNewMenuDelegate::CreateSP(this, &FSequencerObjectBindingNode::AddPropertyMenuItems, KeyableSubMenuPropertyPaths, PropertyNameIndexStart, PropertyNameIndexEnd));

		++MenuDataIndex;
	}
}


void FSequencerObjectBindingNode::HandleLabelsSubMenuCreate(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddWidget(SNew(SSequencerLabelEditor, GetSequencer(), ObjectBinding), FText::GetEmpty(), true);
}


void FSequencerObjectBindingNode::HandlePropertyMenuItemExecute(TArray<UProperty*> PropertyPath)
{
	FSequencer& Sequencer = GetSequencer();
	UObject* BoundObject = FindRepresentativeObject();

	TArray<UObject*> KeyableBoundObjects;
	if (BoundObject != nullptr)
	{
		if (Sequencer.CanKeyProperty(FCanKeyPropertyParams(BoundObject->GetClass(), PropertyPath)))
		{
			KeyableBoundObjects.Add(BoundObject);
		}
	}

	FKeyPropertyParams KeyPropertyParams(KeyableBoundObjects, PropertyPath);
	{
		KeyPropertyParams.KeyParams.bCreateTrackIfMissing = true;
		KeyPropertyParams.KeyParams.bCreateHandleIfMissing = true;
		KeyPropertyParams.KeyParams.bCreateKeyIfUnchanged = true;
		KeyPropertyParams.KeyParams.bCreateKeyIfEmpty = true;
	}

	Sequencer.KeyProperty(KeyPropertyParams);
}


#undef LOCTEXT_NAMESPACE
