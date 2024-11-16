#pragma once

#include <bfsar/Item.h>
#include <bfsar/InnerFile.h>
#include <bfsar/WaveFile.h>

#include <snd/snd_BankFileReader.h>

#include <unordered_map>

class Bank;
class WaveArchive;

class BankFile : public Item, public InnerFile
{
    SEAD_RTTI_OVERRIDE(BankFile, InnerFile);

public:
    class VelocityRegion : public Item
    {
    public:
        VelocityRegion(u8 velocityMin, u8 velocityMax)
            : Item()
            , mVelocityMin(velocityMin)
            , mVelocityMax(velocityMax)

            , mWaveFileRef(this)
            , mOriginalKey(60)
            , mVolume(127)
            , mPan(64)
            , mPitch(1.0f)
            , mIsIgnoreNoteOff(false)
            , mKeyGroup(0)
            , mInterpolationType(0)
            , mAdshrCurve()
        {
            mItemType = ItemType::BankFileVelocityRegion;

            SEAD_ASSERT(mVelocityMin <= 127);
            SEAD_ASSERT(mVelocityMax <= 127);
        }

        void read(const nw::snd::internal::BankFile::VelocityRegion* velocityRegionInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable);
        void drawUI();

        u8 getVelocityMin() const
        {
            return mVelocityMin;
        }

        u8 getVelocityMax() const
        {
            return mVelocityMax;
        }

        const ItemReference& getWaveFileRef() const
        {
            return mWaveFileRef;
        }

        ItemReference& getWaveFileRef()
        {
            return mWaveFileRef;
        }

        u8 getOriginalKey() const
        {
            return mOriginalKey;
        }

        void setOriginalKey(u8 originalKey)
        {
            originalKey = sead::MathCalcCommon<u8>::clampMax(originalKey, 127);
            mOriginalKey = originalKey;
        }

        u8 getVolume() const
        {
            return mVolume;
        }

        void setVolume(u8 volume)
        {
            mVolume = volume;
        }

        u8 getPan() const
        {
            return mPan;
        }

        void setPan(u8 pan)
        {
            pan = sead::MathCalcCommon<u8>::clampMax(pan, 127);
            mPan = pan;
        }

        f32 getPitch() const
        {
            return mPitch;
        }

        void setPitch(f32 pitch)
        {
            pitch = sead::Mathf::clampMin(pitch, 0.0f);
            mPitch = pitch;
        }

        bool getIsIgnoreNoteOff() const
        {
            return mIsIgnoreNoteOff;
        }

        void setIsIgnoreNoteOff(bool ignoreNoteOff)
        {
            mIsIgnoreNoteOff = ignoreNoteOff;
        }

        u8 getKeyGroup() const
        {
            return mKeyGroup;
        }

        void setKeyGroup(u8 keyGroup)
        {
            keyGroup = sead::MathCalcCommon<u8>::clampMax(keyGroup, 16 - 1);
            mKeyGroup = keyGroup;
        }

        u8 getInterpolationType() const
        {
            return mInterpolationType;
        }

        void setInterpolationType(u8 type)
        {
            type = sead::MathCalcCommon<u8>::clampMax(type, 2 - 1);
            mInterpolationType = type;
        }

        const snd::AdshrCurve& getAdshrCurve() const
        {
            return mAdshrCurve;
        }

        void setAdshrCurve(const snd::AdshrCurve& curve)
        {
            mAdshrCurve = curve;

            mAdshrCurve.attack = sead::MathCalcCommon<u8>::clampMax(mAdshrCurve.attack, 127);
            mAdshrCurve.decay = sead::MathCalcCommon<u8>::clampMax(mAdshrCurve.decay, 127);
            mAdshrCurve.sustain = sead::MathCalcCommon<u8>::clampMax(mAdshrCurve.sustain, 127);
            mAdshrCurve.hold = sead::MathCalcCommon<u8>::clampMax(mAdshrCurve.hold, 127);
            mAdshrCurve.release = sead::MathCalcCommon<u8>::clampMax(mAdshrCurve.release, 127);
        }

    private:
        u8 mVelocityMin;
        u8 mVelocityMax;

        ItemReference mWaveFileRef;
        u8 mOriginalKey;
        u8 mVolume;
        u8 mPan;
        f32 mPitch;
        bool mIsIgnoreNoteOff; // Percussion Mode
        u8 mKeyGroup;
        u8 mInterpolationType;
        snd::AdshrCurve mAdshrCurve;

        friend class BankFile;
    };

    class KeyRegion : public Item
    {
    public:
        KeyRegion(u8 keyMin, u8 keyMax)
            : Item()
            , mKeyMin(keyMin)
            , mKeyMax(keyMax)
            , mVelocityRegionList()
        {
            mItemType = ItemType::BankFileKeyRegion;

            SEAD_ASSERT(mKeyMin <= 127);
            SEAD_ASSERT(mKeyMax <= 127);
        }

        void read(const nw::snd::internal::BankFile::KeyRegion* keyRegionInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable);

        const VelocityRegion* getVelocityRegion(u8 velocity) const
        {
            for (const Item* item : mVelocityRegionList)
            {
                SEAD_ASSERT(item->getItemType() == ItemType::BankFileVelocityRegion);
                const VelocityRegion* velocityRegion = static_cast<const VelocityRegion*>(item);

                if (velocityRegion->mVelocityMin <= velocity && velocity <= velocityRegion->mVelocityMax)
                {
                    return velocityRegion;
                }
            }

            return nullptr;
        }

        u8 getKeyMin() const
        {
            return mKeyMin;
        }

        u8 getKeyMax() const
        {
            return mKeyMax;
        }

        const VelocityRegion::List& getVelocityRegionList() const
        {
            return mVelocityRegionList;
        }

        VelocityRegion::List& getVelocityRegionList()
        {
            return mVelocityRegionList;
        }

    private:
        u8 mKeyMin;
        u8 mKeyMax;
        VelocityRegion::List mVelocityRegionList;

        friend class BankFile;
    };

    class Instrument : public Item
    {
    public:
        Instrument()
            : Item()
            , mProgramNo(0)
            , mKeyRegionList()
        {
            mItemType = ItemType::BankFileInstrument;
        }

        void read(const nw::snd::internal::BankFile::Instrument* instrumentInfo, const nw::snd::internal::Util::WaveIdTable& waveIdTable);
        void drawUI();

        s16 getProgramNo() const
        {
            return mProgramNo;
        }

        const KeyRegion* getKeyRegion(u8 key) const
        {
            for (const Item* item : mKeyRegionList)
            {
                SEAD_ASSERT(item->getItemType() == ItemType::BankFileKeyRegion);
                const KeyRegion* keyRegion = static_cast<const KeyRegion*>(item);

                if (keyRegion->mKeyMin <= key && key <= keyRegion->mKeyMax)
                {
                    return keyRegion;
                }
            }

            return nullptr;
        }

        const VelocityRegion* getVelocityRegion(u8 key, u8 velocity) const
        {
            const KeyRegion* keyRegion = getKeyRegion(key);
            if (!keyRegion)
            {
                return nullptr;
            }

            return keyRegion->getVelocityRegion(velocity);
        }

        const KeyRegion::List& getKeyRegionList() const
        {
            return mKeyRegionList;
        }

        KeyRegion::List& getKeyRegionList()
        {
            return mKeyRegionList;
        }

    private:
        s16 mProgramNo;
        KeyRegion::List mKeyRegionList;

        friend class BankFile;
    };

public:
    BankFile()
        : Item()
        , InnerFile()
        , mInstrumentList()

        , mBank(nullptr)
        , mWaveArchive(nullptr)
        , mWaveArchiveWaveFilesIndexes(nullptr)
        , mUpdateWriteInfo(true)
    {
        mItemType = ItemType::BankFile;
    }

    ~BankFile() override;

    bool validate(sead::BufferedSafeString& error) const override;

    void drawUI() override;
    void drawFileUI();

    void setup(sead::Endian::Types endian, u32 version) const
    {
        mEndian = endian;
        mVersion = version;
    }

    void prepare(const Bank* bank, const WaveArchive* warc, const std::unordered_map<const WaveArchive*, std::unordered_map<const WaveFile*, u32>>& waveFilesIndexes, bool updateWriteInfo) const
    {
        SEAD_ASSERT(!mBank);
        mBank = bank;

        SEAD_ASSERT(!mWaveArchive);
        mWaveArchive = warc;

        SEAD_ASSERT(!mWaveArchiveWaveFilesIndexes);

        const auto& it = waveFilesIndexes.find(warc);
        if (it != waveFilesIndexes.end())
        {
            mWaveArchiveWaveFilesIndexes = &it->second;
        }

        mUpdateWriteInfo = updateWriteInfo;
    }

private:
    void doRead(const void* fileAddr) override;
    u32 doWrite(sead::FileHandle* handle, sead::WriteStream* stream, bool isLast) const override;

    bool updateWriteInfo_() const override
    {
        return mUpdateWriteInfo;
    }

public:
    const Instrument* getInstrument(u8 programNo) const
    {
        for (const Item* item : mInstrumentList)
        {
            SEAD_ASSERT(item->getItemType() == ItemType::BankFileInstrument);
            const Instrument* instrument = static_cast<const Instrument*>(item);

            if (instrument->mProgramNo == programNo)
            {
                return instrument;
            }
        }

        return nullptr;
    }

    const Instrument::List& getInstrumentList() const
    {
        return mInstrumentList;
    }

    Instrument::List& getInstrumentList()
    {
        return mInstrumentList;
    }

private:
    Instrument::List mInstrumentList;

    mutable const Bank* mBank;
    mutable const WaveArchive* mWaveArchive;
    mutable const std::unordered_map<const WaveFile*, u32>* mWaveArchiveWaveFilesIndexes;
    mutable bool mUpdateWriteInfo;
};
