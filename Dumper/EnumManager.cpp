#include "EnumManager.h"

namespace EnumInitHelper
{
	template<typename T>
	constexpr inline uint64 GetMaxOfType()
	{
		return (1ull << (sizeof(T) * 0x8ull)) - 1;
	}

	void SetEnumSizeForValue(uint8& Size, uint64 EnumValue)
	{
		if (EnumValue > GetMaxOfType<uint32>()) {
			Size = max(Size, 0x8);
		}
		else if (EnumValue > GetMaxOfType<uint16>()) {
			Size = max(Size, 0x4);
		}
		else if (EnumValue > GetMaxOfType<uint8>()) {
			Size = max(Size, 0x2);
		}
		else {
			Size = max(Size, 0x1);
		}
	}
}

std::string EnumCollisionInfo::GetUniqueName() const
{
	const std::string Name = EnumManager::GetValueName(*this).GetName();

	if (CollisionCount > 0)
		return Name + "_" + std::to_string(CollisionCount - 1);

	return Name;
}

uint64 EnumCollisionInfo::GetValue() const
{
	return MemberValue;
}

uint8 EnumCollisionInfo::GetCollisionCount() const
{
	return CollisionCount;
}

EnumInfoHandle::EnumInfoHandle(const EnumInfo& InInfo)
	: Info(&InInfo)
{
}

uint8 EnumInfoHandle::GetUnderlyingTypeSize() const
{
	return Info->UnderlyingTypeSize;
}

const StringEntry& EnumInfoHandle::GetName() const
{
	return EnumManager::GetEnumName(*Info);
}

CollisionInfoIterator EnumInfoHandle::GetMemberCollisionInfoIterator() const
{
	return CollisionInfoIterator(Info->MemberInfos);
}


void EnumManager::InitInternal()
{
	for (auto Obj : ObjectArray())
	{
		if (Obj.IsA(EClassCastFlags::Struct))
		{
			UEStruct ObjAsStruct = Obj.Cast<UEStruct>();

			for (UEProperty Property : ObjAsStruct.GetProperties())
			{
				if (!Property.IsA(EClassCastFlags::EnumProperty))
					continue;

				UEEnum Enum = Property.Cast<UEEnumProperty>().GetEnum();

				if (!Enum)
					continue;

				EnumInfoOverrides[Enum.GetIndex()].bWasInstanceFound = true;

				/* Check if the size of this enums underlaying type is greater than the default size (0x1) */
				if (Property.Cast<UEEnumProperty>().GetSize() != 0x1) [[unlikely]]
				{
					EnumInfoOverrides[Enum.GetIndex()].UnderlyingTypeSize = Property.Cast<UEEnumProperty>().GetSize();
					continue;
				}

				UEProperty UnderlayingProperty = Property.Cast<UEEnumProperty>().GetUnderlayingProperty();

				if (!UnderlayingProperty) [[unlikely]]
					continue;

				if (UnderlayingProperty.GetSize() != 0x1) [[unlikely]]
					EnumInfoOverrides[Enum.GetIndex()].UnderlyingTypeSize = UnderlayingProperty.GetSize();
			}
		}
		else if (Obj.IsA(EClassCastFlags::Enum))
		{
			UEEnum ObjAsEnum = Obj.Cast<UEEnum>();

			/* Add name to override info */
			EnumInfo& NewOrExistingInfo = EnumInfoOverrides[Obj.GetIndex()];
			NewOrExistingInfo.Name = UniqueEnumNameTable.FindOrAdd(ObjAsEnum.GetEnumPrefixedName()).first;

			uint64 EnumMaxValue = 0x0;

			/* Initialize enum-member names and their collision infos */
			std::vector<std::pair<FName, int64>> NameValuePairs = ObjAsEnum.GetNameValuePairs();
			for (int i = 0; i < NameValuePairs.size(); i++)
			{
				auto& [Name, Value] = NameValuePairs[i];

				EnumMaxValue = max(EnumMaxValue, Value);

				std::string NameWitPrefix = Name.ToString();
				auto [NameIndex, bWasInserted] = UniqueEnumValueNames.FindOrAdd(MakeNameValid(NameWitPrefix.substr(NameWitPrefix.find_last_of("::") + 1)));

				EnumCollisionInfo CurrentEnumValueInfo;
				CurrentEnumValueInfo.MemberName = NameIndex;
				CurrentEnumValueInfo.MemberValue = Value;

				if (bWasInserted) [[likely]]
				{
					NewOrExistingInfo.MemberInfos.push_back(CurrentEnumValueInfo);
					continue;
				}

				/* A value with this name exists globally, now check if it also exists localy (aka. is duplicated) */
				for (int j = 0; j < i; j++)
				{
					EnumCollisionInfo& CrosscheckedInfo = NewOrExistingInfo.MemberInfos[j];

					if (CrosscheckedInfo.MemberName != NameIndex) [[likely]]
						continue;

					/* Duplicate was found */
					CurrentEnumValueInfo.CollisionCount = CrosscheckedInfo.CollisionCount + 1;
					break;
				}

				NewOrExistingInfo.MemberInfos.push_back(CurrentEnumValueInfo);
			}

			/* Initialize the size based on the highest value contained by this enum */
			if (!NewOrExistingInfo.bWasEnumSizeInitialized && !NewOrExistingInfo.bWasInstanceFound)
			{

				EnumInitHelper::SetEnumSizeForValue(NewOrExistingInfo.UnderlyingTypeSize, EnumMaxValue);
				NewOrExistingInfo.bWasEnumSizeInitialized = true;
			}
		}
	}
}

void EnumManager::Init()
{
	if (bIsInitialized)
		return;

	bIsInitialized = true;

	EnumInfoOverrides.reserve(0x1000);

	InitInternal();
}