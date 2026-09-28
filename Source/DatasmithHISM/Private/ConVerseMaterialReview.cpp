#include "ConVerseMaterialReview.h"
#include "ConVerseImportProcessing.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "Factories/DataTableFactory.h"
#include "Framework/Application/SlateApplication.h"
#include "PropertyCustomizationHelpers.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "ConVerseMaterialReview"

namespace ConVerseMaterialReview
{
	class SReview final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SReview) {} SLATE_END_ARGS()
		void Construct(const FArguments&, UConVerseImportRecipe* InRecipe, const TArray<FConVerseAppearanceReviewRow>& InRows, FSimpleDelegate InChanged)
		{
			Recipe.Reset(InRecipe);
			Rows = InRows;
			Changed = InChanged;
			Refresh();
			ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Instructions", "Names suggest candidates. Review appearance properties, texture evidence, and physical scale before approving an exact variant. Imported materials remain the default. Catalog recognition does not approve a replacement."))]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("CreateTables", "Create project tables")).OnClicked(this, &SReview::CreateTables)]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Catalog", "Open catalog / CSV")).OnClicked_Lambda([this] { OpenTable(Recipe->Processing.AppearanceCatalog); return FReply::Handled(); })]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Mappings", "Open approvals / CSV")).OnClicked_Lambda([this] { OpenTable(Recipe->Processing.MaterialMappings); return FReply::Handled(); })]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[SNew(SSearchBox).HintText(LOCTEXT("Search", "Search source appearances or fingerprints")).OnTextChanged_Lambda([this](const FText& Text) { Search = Text.ToString(); Refresh(); })]
				+ SVerticalBox::Slot().FillHeight(0.4f).Padding(8)
				[
					SAssignNew(List, SListView<TSharedPtr<FConVerseAppearanceReviewRow>>).ListItemsSource(&Filtered)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow_Lambda([](TSharedPtr<FConVerseAppearanceReviewRow> Row, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
					{
						return SNew(STableRow<TSharedPtr<FConVerseAppearanceReviewRow>>, Owner)
						[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s | %d source elements / %d mesh default slots | %s"), *Row->Name, Row->AffectedSourceElements, Row->ReferencedSlots, *Row->SourceElement)))];
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<FConVerseAppearanceReviewRow> Row, ESelectInfo::Type)
					{
						Selected = Row;
						Target.Reset();
						CatalogId = Row && Row->CatalogCandidates.Num() == 1 ? Row->CatalogCandidates[0] : FString();
						Status = TEXT("Unmapped; imported material will be preserved.");
						if (UDataTable* Table = Recipe->Processing.MaterialMappings.LoadSynchronous(); Row && Table && Table->GetRowStruct() == FConVerseMaterialMappingRow::StaticStruct())
							for (const auto& Pair : Table->GetRowMap())
							{
								const auto& Mapping = *reinterpret_cast<const FConVerseMaterialMappingRow*>(Pair.Value);
								if (Mapping.SourceFingerprint == Row->Fingerprint)
								{
									CatalogId = Mapping.CatalogId; Target = Mapping.Replacement;
									Status = Mapping.bApproved ? TEXT("Approved for this exact source appearance variant.") : TEXT("Candidate, awaiting review.");
								}
							}
						CatalogText->SetText(FText::FromString(CatalogId));
					})
				]
				+ SVerticalBox::Slot().FillHeight(0.4f).Padding(8)
				[SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(Selected ? Selected->Evidence + TEXT("\nCatalog candidates (review required): ") + FString::Join(Selected->CatalogCandidates, TEXT(", ")) : TEXT("Select an appearance.")); })]]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[SAssignNew(CatalogText, SEditableTextBox).HintText(LOCTEXT("CatalogID", "Catalog ID")).OnTextChanged_Lambda([this](const FText& Text) { CatalogId = Text.ToString(); })]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[SNew(SObjectPropertyEntryBox).AllowedClass(UMaterialInterface::StaticClass()).ObjectPath_Lambda([this] { return Target.ToSoftObjectPath().ToString(); }).OnObjectChanged_Lambda([this](const FAssetData& Asset) { Target = Cast<UMaterialInterface>(Asset.GetAsset()); })]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Observe", "Add observed appearance to catalog")).OnClicked(this, &SReview::Observe)]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Approve", "Approve this variant")).OnClicked(this, &SReview::Approve)]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Revoke", "Revoke approval")).OnClicked(this, &SReview::Revoke)]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("SourcePreview", "Inspect imported source material")).OnClicked_Lambda([this] { if (Selected && Selected->ImportedMaterial.TryLoad()) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Selected->ImportedMaterial.ResolveObject()); else Status = TEXT("Import the source to inspect its Unreal material preview. Exported appearance properties remain available above before import."); return FReply::Handled(); })]
					+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Target", "Inspect target material")).OnClicked_Lambda([this] { if (Target.LoadSynchronous()) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Target.Get()); return FReply::Handled(); })]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(8)
				[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(Status); })]
			];
		}
	private:
		void Refresh()
		{
			Filtered.Reset();
			for (const auto& Row : Rows)
				if (Search.IsEmpty() || (Row.Name + Row.SourceElement + Row.Fingerprint).Contains(Search)) Filtered.Add(MakeShared<FConVerseAppearanceReviewRow>(Row));
			if (List) List->RequestListRefresh();
		}
		void OpenTable(const TSoftObjectPtr<UDataTable>& Table)
		{
			if (Table.LoadSynchronous()) GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Table.Get());
			else Status = TEXT("Choose tables in the import settings or create project tables first.");
		}
		FReply CreateTables()
		{
			auto& Assets = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			auto Create = [&](TSoftObjectPtr<UDataTable>& Table, UScriptStruct* Type, const TCHAR* Name)
			{
				if (!Table.IsNull()) return;
				UDataTableFactory* Factory = NewObject<UDataTableFactory>(); Factory->Struct = Type;
				FString Package, AssetName;
				Assets.CreateUniqueAssetName(FString(TEXT("/Game/DatasmithImportSettings/")) + Name, TEXT(""), Package, AssetName);
				Table = Cast<UDataTable>(Assets.CreateAsset(AssetName, FPackageName::GetLongPackagePath(Package), UDataTable::StaticClass(), Factory));
			};
			Create(Recipe->Processing.AppearanceCatalog, FConVerseAppearanceCatalogRow::StaticStruct(), TEXT("AppearanceCatalog"));
			Create(Recipe->Processing.MaterialMappings, FConVerseMaterialMappingRow::StaticStruct(), TEXT("ApprovedMappings"));
			Changed.ExecuteIfBound();
			Status = TEXT("Project tables created. Save these assets and the import preset to retain them. DataTable editors provide CSV import/export. No appearances have been approved.");
			return FReply::Handled();
		}
		FReply Observe()
		{
			UDataTable* Table = Recipe->Processing.AppearanceCatalog.LoadSynchronous();
			if (!Selected || CatalogId.IsEmpty() || !Table || Table->GetRowStruct() != FConVerseAppearanceCatalogRow::StaticStruct())
			{ Status = TEXT("Select an appearance, enter a catalog ID, and choose a valid catalog table."); return FReply::Handled(); }
			for (const auto& Pair : Table->GetRowMap())
				if (reinterpret_cast<const FConVerseAppearanceCatalogRow*>(Pair.Value)->CatalogId == CatalogId)
				{ Status = TEXT("That catalog ID already exists. Inspect the existing entry; it was not replaced."); return FReply::Handled(); }
			FConVerseAppearanceCatalogRow Row;
			Row.CatalogId = CatalogId; Row.AppearanceName = Selected->Name; Row.SourceIdentity = Selected->SourceElement;
			Row.ReferenceFingerprint = Selected->Fingerprint; Row.Evidence = Selected->Evidence;
			Row.LibraryVersion = TEXT("Observed export; Autodesk stock-library identity unverified");
			Table->Modify(); Table->AddRow(FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits)), Row); Table->MarkPackageDirty();
			Changed.ExecuteIfBound();
			Status = TEXT("Observed appearance added, unsaved. This records provenance without claiming Autodesk stock-library identity or approving a replacement.");
			return FReply::Handled();
		}
		FReply Approve()
		{
			if (Selected && ConVerseImportProcessing::ApproveAppearance(*Selected, CatalogId, Target.LoadSynchronous(), Recipe->Processing, Status))
			{
				Changed.ExecuteIfBound();
				Status = TEXT("Variant approved, unsaved. Save the mapping table. Enable Apply Approved Materials in import settings, then Analyze and rebuild to apply it.");
			}
			return FReply::Handled();
		}
		FReply Revoke()
		{
			UDataTable* Table = Recipe->Processing.MaterialMappings.LoadSynchronous();
			if (Selected && Table && Table->GetRowStruct() == FConVerseMaterialMappingRow::StaticStruct())
			{
				Table->Modify();
				for (const auto& Pair : Table->GetRowMap())
				{
					auto& Row = *reinterpret_cast<FConVerseMaterialMappingRow*>(Pair.Value);
					if (Row.SourceFingerprint == Selected->Fingerprint) { Row.bApproved = false; ++Row.Revision; }
				}
				Table->MarkPackageDirty(); Changed.ExecuteIfBound();
				Status = TEXT("Approval revoked, unsaved. Save the mapping table; rebuild explicitly to restore source materials.");
			}
			return FReply::Handled();
		}
		TStrongObjectPtr<UConVerseImportRecipe> Recipe;
		TArray<FConVerseAppearanceReviewRow> Rows;
		TArray<TSharedPtr<FConVerseAppearanceReviewRow>> Filtered;
		TSharedPtr<FConVerseAppearanceReviewRow> Selected;
		TSharedPtr<SListView<TSharedPtr<FConVerseAppearanceReviewRow>>> List;
		TSharedPtr<SEditableTextBox> CatalogText;
		TSoftObjectPtr<UMaterialInterface> Target;
		FString Search, CatalogId, Status;
		FSimpleDelegate Changed;
	};

	TSharedRef<SWindow> Open(UConVerseImportRecipe* Recipe, const TArray<FConVerseAppearanceReviewRow>& Appearances, FSimpleDelegate OnChanged)
	{
		TSharedRef<SWindow> Window = SNew(SWindow).Title(LOCTEXT("Title", "Review material appearances")).ClientSize(FVector2D(1050, 760))
			[SNew(SReview, Recipe, Appearances, OnChanged)];
		FSlateApplication::Get().AddWindow(Window);
		return Window;
	}
}

#undef LOCTEXT_NAMESPACE
