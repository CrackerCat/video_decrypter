#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#ifdef OS_WIN
#include <windows.h>
#endif
#include <libgen.h>

#include "../lib/libbento4/Core/Ap4.h"
#include "../lib/inputstream.adaptive/parser/DASHTree.h"
#include "../lib/inputstream.adaptive/common/AdaptiveStream.h"
#include "../lib/inputstream.adaptive/SSD_dll.h"
#include "../lib/inputstream.adaptive/helpers.h"
#ifdef OS_WIN
#include "../lib/p8-platform/src/windows/dlfcn-win32.h"
#else
#include <dlfcn.h>
#endif

#define SAFE_DELETE(p)   do { delete (p); (p)=NULL; } while (0)
#define DVD_TIME_BASE 1000000
#define DVD_NOPTS_VALUE 0xFFF0000000000000


std::ofstream file_decrypted_data;
std::ifstream file_fragment;
std::ifstream file_mpd;
std::string encrypted_file;
std::string stream_id_str;
std::string info_path;
std::string decrypted_path;
std::string profile_path;
AP4_Position stream_start_pos = 0;

#ifdef OS_WIN
bool StartProcess(const char *proc)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory( &pi, sizeof(pi) );

    if(!CreateProcess(NULL, const_cast<char*>(proc), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        printf("CreateProcess failed (%d).\n", GetLastError());
        return false;
    }

    WaitForSingleObject( pi.hProcess, INFINITE );

    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
    return true;
}
#endif

static const AP4_Track::Type TIDC[adaptive::AdaptiveTree::STREAM_TYPE_COUNT] =
{
    AP4_Track::TYPE_UNKNOWN,
    AP4_Track::TYPE_VIDEO,
    AP4_Track::TYPE_AUDIO,
    AP4_Track::TYPE_SUBTITLES
};

typedef struct CRYPTO_INFO
{
    enum CRYPTO_KEY_SYSTEM : uint8_t
    {
        CRYPTO_KEY_SYSTEM_NONE = 0,
        CRYPTO_KEY_SYSTEM_WIDEVINE,
        CRYPTO_KEY_SYSTEM_PLAYREADY,
        CRYPTO_KEY_SYSTEM_COUNT
    } m_CryptoKeySystem;                 /*!< @brief keysystem for encrypted media, KEY_SYSTEM_NONE for unencrypted media */

    static const uint8_t FLAG_SECURE_DECODER = 1; /*!< @brief is set in flags if decoding has to be done in TEE environment */

    uint8_t flags;
    uint16_t m_CryptoSessionIdSize;      /*!< @brief The size of the crypto session key id */
    const char *m_CryptoSessionId;       /*!< @brief The crypto session key id */
} CRYPTO_INFO;

enum STREAMCODEC_PROFILE
{
    CodecProfileUnknown = 0,
    CodecProfileNotNeeded,
    H264CodecProfileBaseline,
    H264CodecProfileMain,
    H264CodecProfileExtended,
    H264CodecProfileHigh,
    H264CodecProfileHigh10,
    H264CodecProfileHigh422,
    H264CodecProfileHigh444Predictive
};

struct INPUTSTREAM_INFO
{
    enum STREAM_TYPE
    {
        TYPE_NONE,
        TYPE_VIDEO,
        TYPE_AUDIO,
        TYPE_SUBTITLE,
        TYPE_TELETEXT
    } m_streamType;

    enum Codec_FEATURES : uint32_t
    {
        FEATURE_DECODE = 1
    };
    uint32_t m_features;

    enum STREAM_FLAGS : uint32_t
    {
        FLAG_NONE = 0x0000,
        FLAG_DEFAULT = 0x0001,
        FLAG_DUB = 0x0002,
        FLAG_ORIGINAL = 0x0004,
        FLAG_COMMENT = 0x0008,
        FLAG_LYRICS = 0x0010,
        FLAG_KARAOKE = 0x0020,
        FLAG_FORCED = 0x0040,
        FLAG_HEARING_IMPAIRED = 0x0080,
        FLAG_VISUAL_IMPAIRED = 0x0100
    };
    uint32_t m_flags;

    char m_name[256];                    /*!< @brief (optinal) name of the stream, \0 for default handling */
    char m_codecName[32];                /*!< @brief (required) name of codec according to ffmpeg */
    char m_codecInternalName[32];        /*!< @brief (optional) internal name of codec (selectionstream info) */
    STREAMCODEC_PROFILE m_codecProfile;  /*!< @brief (optional) the profile of the codec */
    unsigned int m_pID;                  /*!< @brief (required) physical index */

    const uint8_t *m_ExtraData;
    unsigned int m_ExtraSize;

    char m_language[4];                  /*!< @brief ISO 639 3-letter language code (empty string if undefined) */

    unsigned int m_FpsScale;             /*!< @brief Scale of 1000 and a rate of 29970 will result in 29.97 fps */
    unsigned int m_FpsRate;
    unsigned int m_Height;               /*!< @brief height of the stream reported by the demuxer */
    unsigned int m_Width;                /*!< @brief width of the stream reported by the demuxer */
    float m_Aspect;                      /*!< @brief display aspect of stream */

    unsigned int m_Channels;             /*!< @brief (required) amount of channels */
    unsigned int m_SampleRate;           /*!< @brief (required) sample rate */
    unsigned int m_BitRate;              /*!< @brief (required) bit rate */
    unsigned int m_BitsPerSample;        /*!< @brief (required) bits per sample */
    unsigned int m_BlockAlign;

    CRYPTO_INFO m_cryptoInfo;
};

class SampleReader
{
public:
    virtual ~SampleReader() = default;
    virtual bool EOS()const = 0;
    virtual uint64_t  DTS()const = 0;
    virtual uint64_t  PTS()const = 0;
    virtual uint64_t  Elapsed(uint64_t basePTS) = 0;
    virtual AP4_Result Start(bool &bStarted) = 0;
    virtual AP4_Result ReadSample() = 0;
    virtual void Reset(bool bEOS) = 0;
    virtual bool GetInformation(INPUTSTREAM_INFO &info) = 0;
    virtual bool TimeSeek(uint64_t pts, bool preceeding) = 0;
    virtual void SetPTSOffset(uint64_t offset) = 0;
    virtual bool GetNextFragmentInfo(uint64_t &ts, uint64_t &dur) = 0;
    virtual uint32_t GetTimeScale()const = 0;
    virtual AP4_UI32 GetStreamId()const = 0;
    virtual AP4_Size GetSampleDataSize()const = 0;
    virtual const AP4_Byte *GetSampleData()const = 0;
    virtual uint64_t GetDuration()const = 0;
    virtual bool IsEncrypted()const = 0;
    virtual void AddStreamType(INPUTSTREAM_INFO::STREAM_TYPE type, uint16_t sid) {};
    virtual void SetStreamType(INPUTSTREAM_INFO::STREAM_TYPE type, uint16_t sid) {};
    virtual bool RemoveStreamType(INPUTSTREAM_INFO::STREAM_TYPE type)
    {
        return true;
    };
};

class AP4_DASHStream : public AP4_ByteStream
{
public:
  // Constructor
  AP4_DASHStream(adaptive::AdaptiveStream *stream) :stream_(stream){};

  // AP4_ByteStream methods
  AP4_Result ReadPartial(void*    buffer,
    AP4_Size  bytesToRead,
    AP4_Size& bytesRead) override
  {
    bytesRead = stream_->read(buffer, bytesToRead);
    return bytesRead > 0 ? AP4_SUCCESS : AP4_ERROR_READ_FAILED;
  };
  AP4_Result WritePartial(const void* buffer,
    AP4_Size    bytesToWrite,
    AP4_Size&   bytesWritten) override
  {
    /* unimplemented */
    return AP4_ERROR_NOT_SUPPORTED;
  };
  AP4_Result Seek(AP4_Position position) override
  {
    return stream_->seek(position) ? AP4_SUCCESS : AP4_ERROR_NOT_SUPPORTED;
  };
  AP4_Result Tell(AP4_Position& position) override
  {
    position = stream_->tell();
    return AP4_SUCCESS;
  };
  AP4_Result GetSize(AP4_LargeSize& size) override
  {
    /* unimplemented */
    return AP4_ERROR_NOT_SUPPORTED;
  };
  // AP4_Referenceable methods
  void AddReference() override {};
  void Release()override      {};
  bool waitingForSegment() const { return stream_->waitingForSegment(); }
protected:
  // members
  adaptive::AdaptiveStream *stream_;
};

class MyAdaptiveStream : public adaptive::AdaptiveStream
{
public:
    MyAdaptiveStream(adaptive::AdaptiveTree &tree, adaptive::AdaptiveTree::StreamType type)
        :adaptive::AdaptiveStream(tree, type) {};
protected:
  virtual bool download(const char* url, const std::map<std::string, std::string> &mediaHeaders) override;
};

struct CDMSESSION
{
    SSD::SSD_DECRYPTER::SSD_CAPS decrypter_caps_;
    AP4_CencSingleSampleDecrypter *single_sample_decryptor_;
    const char *cdm_session_str_;
    bool shared_single_sample_decryptor_;
};

std::vector<CDMSESSION> cdm_sessions_;

struct STREAM
{
    STREAM(adaptive::AdaptiveTree &t, adaptive::AdaptiveTree::StreamType s) :enabled(false), encrypted(false), mainId_(0), current_segment_(0), stream_(t, s), input_(0), input_file_(0), reader_(0), segmentChanged(false)
    {
        memset(&info_, 0, sizeof(info_));
    };
    ~STREAM()
    {
        disable();
        free((void*)info_.m_ExtraData);
    };
    void disable()
    {
        if (enabled)
        {
            stream_.stop();
            SAFE_DELETE(reader_);
            SAFE_DELETE(input_file_);
            SAFE_DELETE(input_);
            enabled = encrypted = false;
            mainId_ = 0;
        }
    }

    bool enabled, encrypted;
    uint16_t mainId_;
    uint32_t current_segment_;
    MyAdaptiveStream stream_;
    AP4_ByteStream *input_;
    AP4_File *input_file_;
    INPUTSTREAM_INFO info_;
    SampleReader *reader_;
    bool segmentChanged;
};

std::vector<STREAM*> streams_;
adaptive::AdaptiveTree *adaptiveTree_;
uint64_t elapsed_time_;
uint16_t m_IncludedStreams[16];
bool changed_;

const SSD::SSD_DECRYPTER::SSD_CAPS &GetDecrypterCaps(unsigned int nIndex) { return cdm_sessions_[nIndex].decrypter_caps_; };

STREAM *GetStream(unsigned int sid) { return sid - 1 < streams_.size() ? streams_[sid - 1] : 0; };

bool CheckChange(bool bSet = false){ bool ret = changed_; changed_ = bSet; return ret; };

AP4_CencSingleSampleDecrypter * GetSingleSampleDecryptor(unsigned int nIndex){ return cdm_sessions_[nIndex].single_sample_decryptor_; };

std::uint16_t GetVideoWidth() {return 8192;}
std::uint16_t GetVideoHeight() {return 8192;}

const AP4_UI08 *GetDefaultKeyId(const uint16_t index)
{
  static const AP4_UI08 default_key[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
  if (adaptiveTree_->psshSets_[index].defaultKID_.size() == 16)
    return reinterpret_cast<const AP4_UI08 *>(adaptiveTree_->psshSets_[index].defaultKID_.data());
  return default_key;
}

AP4_Movie *PrepareStream(STREAM *stream)
{
  if (!adaptiveTree_->prepareRepresentation(const_cast<adaptive::AdaptiveTree::Representation *>(stream->stream_.getRepresentation())))
    return nullptr;

  if (stream->stream_.getRepresentation()->containerType_ == adaptive::AdaptiveTree::CONTAINERTYPE_MP4
    && (stream->stream_.getRepresentation()->flags_ & adaptive::AdaptiveTree::Representation::INITIALIZATION_PREFIXED) == 0
    && stream->stream_.getRepresentation()->get_initialization() == nullptr)
  {
    //We'll create a Movie out of the things we got from manifest file
    //note: movie will be deleted in destructor of stream->input_file_
    AP4_Movie *movie = new AP4_Movie();

    AP4_SyntheticSampleTable* sample_table = new AP4_SyntheticSampleTable();

    AP4_SampleDescription *sample_descryption;
    if (strcmp(stream->info_.m_codecName, "h264") == 0)
    {
      const std::string &extradata(stream->stream_.getRepresentation()->codec_private_data_);
      AP4_MemoryByteStream ms((const uint8_t*)extradata.data(), extradata.size());
      AP4_AvccAtom *atom = AP4_AvccAtom::Create(AP4_ATOM_HEADER_SIZE + extradata.size(), ms);
      sample_descryption = new AP4_AvcSampleDescription(AP4_SAMPLE_FORMAT_AVC1, stream->info_.m_Width, stream->info_.m_Height, 0, nullptr, atom);
    }
    else if (strcmp(stream->info_.m_codecName, "srt") == 0)
      sample_descryption = new AP4_SampleDescription(AP4_SampleDescription::TYPE_SUBTITLES, AP4_SAMPLE_FORMAT_STPP, 0);
    else
      sample_descryption = new AP4_SampleDescription(AP4_SampleDescription::TYPE_UNKNOWN, 0, 0);

    if (stream->stream_.getRepresentation()->get_psshset() > 0)
    {
      AP4_ContainerAtom schi(AP4_ATOM_TYPE_SCHI);
      schi.AddChild(new AP4_TencAtom(AP4_CENC_ALGORITHM_ID_CTR, 8, GetDefaultKeyId(stream->stream_.getRepresentation()->get_psshset())));
      sample_descryption = new AP4_ProtectedSampleDescription(0, sample_descryption, 0, AP4_PROTECTION_SCHEME_TYPE_PIFF, 0, "", &schi);
    }
    sample_table->AddSampleDescription(sample_descryption);

    movie->AddTrack(new AP4_Track(TIDC[stream->stream_.get_type()], sample_table, ~0, stream->stream_.getRepresentation()->timescale_, 0, stream->stream_.getRepresentation()->timescale_, 0, "", 0, 0));
    //Create a dumy MOOV Atom to tell Bento4 its a fragmented stream
    AP4_MoovAtom *moov = new AP4_MoovAtom();
    moov->AddChild(new AP4_ContainerAtom(AP4_ATOM_TYPE_MVEX));
    movie->SetMoovAtom(moov);
    return movie;
  }
  return nullptr;
}

void CheckFragmentDuration(STREAM &stream)
{
  uint64_t nextTs, nextDur;
  if (stream.segmentChanged && stream.reader_->GetNextFragmentInfo(nextTs, nextDur))
    adaptiveTree_->SetFragmentDuration(
      stream.stream_.getAdaptationSet(),
      stream.stream_.getRepresentation(),
      stream.stream_.getSegmentPos(),
      nextTs,
      static_cast<uint32_t>(nextDur),
      stream.reader_->GetTimeScale());
  stream.segmentChanged = false;
}

SampleReader *GetNextSample()
{
  STREAM *res(0), *waiting(0);
  for (std::vector<STREAM*>::const_iterator b(streams_.begin()), e(streams_.end()); b != e; ++b)
  {
    bool bStarted(false);
    if ((*b)->enabled && (*b)->reader_ && !(*b)->reader_->EOS()
    && AP4_SUCCEEDED((*b)->reader_->Start(bStarted))
    && (!res || (*b)->reader_->DTS() < res->reader_->DTS()))
      ((*b)->stream_.waitingForSegment(true) ? waiting : res) = *b;

    if (bStarted && ((*b)->reader_->GetInformation((*b)->info_)))
      changed_ = true;
  }

  if (res)
  {
    CheckFragmentDuration(*res);
    if (res->reader_->GetInformation(res->info_))
      changed_ = true;
    if (res->reader_->PTS() != DVD_NOPTS_VALUE)
      elapsed_time_ = res->reader_->Elapsed(res->stream_.GetAbsolutePTSOffset());
    return res->reader_;
  }
  return 0;
}

void adaptive::AdaptiveStreamObserver::OnSegmentChanged(adaptive::AdaptiveStream *stream)
{
  for (std::vector<STREAM*>::iterator s(streams_.begin()), e(streams_.end()); s != e; ++s)
    if (&(*s)->stream_ == stream)
    {
      if((*s)->reader_)
        (*s)->reader_->SetPTSOffset((*s)->stream_.GetCurrentPTSOffset());
      (*s)->segmentChanged = true;
      break;
    }
}

void adaptive::AdaptiveStreamObserver::OnStreamChange(adaptive::AdaptiveStream *stream){}

void UpdateStream(STREAM &stream, const SSD::SSD_DECRYPTER::SSD_CAPS &caps)
{
  const adaptive::AdaptiveTree::Representation *rep(stream.stream_.getRepresentation());

  stream.info_.m_name[0] = 0;
  stream.info_.m_Width = rep->width_;
  stream.info_.m_Height = rep->height_;
  stream.info_.m_Aspect = rep->aspect_;

  if (stream.info_.m_Aspect == 0.0f && stream.info_.m_Height)
    stream.info_.m_Aspect = (float)stream.info_.m_Width / stream.info_.m_Height;
  stream.encrypted = rep->get_psshset() > 0;

  if (!stream.info_.m_ExtraSize && rep->codec_private_data_.size())
  {
    std::string annexb;
    const std::string *res(&annexb);

    if ((caps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED)
      && stream.info_.m_streamType == INPUTSTREAM_INFO::TYPE_VIDEO)
    {
     printf("UpdateStream: Convert avc -> annexb\n");
      annexb = avc_to_annexb(rep->codec_private_data_);
    }
    else
      res = &rep->codec_private_data_;

    stream.info_.m_ExtraSize = res->size();
    stream.info_.m_ExtraData = (const uint8_t*)malloc(stream.info_.m_ExtraSize);
    memcpy((void*)stream.info_.m_ExtraData, res->data(), stream.info_.m_ExtraSize);
  }

  // we currently use only the first track!
  std::string::size_type pos = rep->codecs_.find(",");
  if (pos == std::string::npos)
    pos = rep->codecs_.size();

  strncpy(stream.info_.m_codecInternalName, rep->codecs_.c_str(), pos);
  stream.info_.m_codecInternalName[pos] = 0;

  if (rep->codecs_.find("mp4a") == 0
  || rep->codecs_.find("aac") == 0)
    strcpy(stream.info_.m_codecName, "aac");
  else if (rep->codecs_.find("ec-3") == 0 || rep->codecs_.find("ac-3") == 0)
    strcpy(stream.info_.m_codecName, "eac3");
  else if (rep->codecs_.find("avc") == 0
  || rep->codecs_.find("h264") == 0)
    strcpy(stream.info_.m_codecName, "h264");
  else if (rep->codecs_.find("hev") == 0 || rep->codecs_.find("hvc") == 0)
    strcpy(stream.info_.m_codecName, "hevc");
  else if (rep->codecs_.find("vp9") == 0)
    strcpy(stream.info_.m_codecName, "vp9");
  else if (rep->codecs_.find("opus") == 0)
    strcpy(stream.info_.m_codecName, "opus");
  else if (rep->codecs_.find("vorbis") == 0)
    strcpy(stream.info_.m_codecName, "vorbis");
  else if (rep->codecs_.find("stpp") == 0 || rep->codecs_.find("ttml") == 0)
    strcpy(stream.info_.m_codecName, "srt");

  stream.info_.m_FpsRate = rep->fpsRate_;
  stream.info_.m_FpsScale = rep->fpsScale_;
  stream.info_.m_SampleRate = rep->samplingRate_;
  stream.info_.m_Channels = rep->channelCount_;
  stream.info_.m_BitRate = rep->bandwidth_;
}

/*******************************************************
|   CodecHandler
********************************************************/

class CodecHandler
{
public:
  CodecHandler(AP4_SampleDescription *sd)
    : sample_description(sd)
    , naluLengthSize(0)
    , pictureId(0)
    , pictureIdPrev(0xFF)
  {};
  virtual ~CodecHandler() {};

  virtual void UpdatePPSId(AP4_DataBuffer const&){};
  virtual bool GetInformation(INPUTSTREAM_INFO &info)
  {
    AP4_GenericAudioSampleDescription* asd(nullptr);
    if (sample_description && (asd = dynamic_cast<AP4_GenericAudioSampleDescription*>(sample_description)))
    {
      if (asd->GetChannelCount() != info.m_Channels
        || asd->GetSampleRate() != info.m_SampleRate
        || asd->GetSampleSize() != info.m_BitsPerSample)
      {
        info.m_Channels = asd->GetChannelCount();
        info.m_SampleRate = asd->GetSampleRate();
        info.m_BitsPerSample = asd->GetSampleSize();
        return true;
      }
    }
    return false;
  };
  virtual bool ExtraDataToAnnexB() { return false; };
  virtual STREAMCODEC_PROFILE GetProfile() { return STREAMCODEC_PROFILE::CodecProfileNotNeeded; };
  virtual bool Transform(AP4_DataBuffer &buf, AP4_UI64 timescale) { return false; };
  virtual bool ReadNextSample(AP4_Sample &sample, AP4_DataBuffer &buf) { return false; };
  virtual void SetPTSOffset(AP4_UI64 offset) { };
  virtual bool TimeSeek(AP4_UI64 seekPos) { return true; };
  virtual void Reset() { };

  AP4_SampleDescription *sample_description;
  AP4_DataBuffer extra_data;
  AP4_UI08 naluLengthSize;
  AP4_UI08 pictureId, pictureIdPrev;
};

/***********************   AVC   ************************/

class AVCCodecHandler : public CodecHandler
{
public:
  AVCCodecHandler(AP4_SampleDescription *sd)
    : CodecHandler(sd)
    , countPictureSetIds(0)
    , needSliceInfo(false)
  {
    unsigned int width(0), height(0);
    if (AP4_VideoSampleDescription *video_sample_description = AP4_DYNAMIC_CAST(AP4_VideoSampleDescription, sample_description))
    {
      width = video_sample_description->GetWidth();
      height = video_sample_description->GetHeight();
    }
    if (AP4_AvcSampleDescription *avc = AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, sample_description))
    {
      extra_data.SetData(avc->GetRawBytes().GetData(), avc->GetRawBytes().GetDataSize());
      countPictureSetIds = avc->GetPictureParameters().ItemCount();
      naluLengthSize = avc->GetNaluLengthSize();
      needSliceInfo = (countPictureSetIds > 1 || !width || !height);
      switch (avc->GetProfile())
      {
      case AP4_AVC_PROFILE_BASELINE:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileBaseline;
        break;
      case AP4_AVC_PROFILE_MAIN:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileMain;
        break;
      case AP4_AVC_PROFILE_EXTENDED:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileExtended;
        break;
      case AP4_AVC_PROFILE_HIGH:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh;
        break;
      case AP4_AVC_PROFILE_HIGH_10:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh10;
        break;
      case AP4_AVC_PROFILE_HIGH_422:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh422;
        break;
      case AP4_AVC_PROFILE_HIGH_444:
        codecProfile = STREAMCODEC_PROFILE::H264CodecProfileHigh444Predictive;
        break;
      default:
        codecProfile = STREAMCODEC_PROFILE::CodecProfileUnknown;
        break;
      }
    }
  }

  virtual bool ExtraDataToAnnexB() override
  {
    if (AP4_AvcSampleDescription *avc = AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, sample_description))
    {
      //calculate the size for annexb
      size_t sz(0);
      AP4_Array<AP4_DataBuffer>& pps(avc->GetPictureParameters());
      for (unsigned int i(0); i < pps.ItemCount(); ++i)
        sz += 4 + pps[i].GetDataSize();
      AP4_Array<AP4_DataBuffer>& sps(avc->GetSequenceParameters());
      for (unsigned int i(0); i < sps.ItemCount(); ++i)
        sz += 4 + sps[i].GetDataSize();

      extra_data.SetDataSize(sz);
      uint8_t *cursor(extra_data.UseData());

      for (unsigned int i(0); i < sps.ItemCount(); ++i)
      {
        cursor[0] = cursor[1] = cursor[2] = 0; cursor[3] = 1;
        memcpy(cursor + 4, sps[i].GetData(), sps[i].GetDataSize());
        cursor += sps[i].GetDataSize() + 4;
      }
      for (unsigned int i(0); i < pps.ItemCount(); ++i)
      {
        cursor[0] = cursor[1] = cursor[2] = 0; cursor[3] = 1;
        memcpy(cursor + 4, pps[i].GetData(), pps[i].GetDataSize());
        cursor += pps[i].GetDataSize() + 4;
      }
      return true;
    }
    return false;
  }

  virtual void UpdatePPSId(AP4_DataBuffer const &buffer) override
  {
    if (!needSliceInfo)
      return;

    //Search the Slice header NALU
    const AP4_UI08 *data(buffer.GetData());
    unsigned int data_size(buffer.GetDataSize());
    for (; data_size;)
    {
      // sanity check
      if (data_size < naluLengthSize)
        break;

      // get the next NAL unit
      AP4_UI32 nalu_size;
      switch (naluLengthSize) {
      case 1:nalu_size = *data++; data_size--; break;
      case 2:nalu_size = AP4_BytesToInt16BE(data); data += 2; data_size -= 2; break;
      case 4:nalu_size = AP4_BytesToInt32BE(data); data += 4; data_size -= 4; break;
      default: data_size = 0; nalu_size = 1; break;
      }
      if (nalu_size > data_size)
        break;

      // Stop further NALU processing
      if (countPictureSetIds < 2)
        needSliceInfo = false;

      unsigned int nal_unit_type = *data & 0x1F;

      if (
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_OF_NON_IDR_PICTURE ||
        nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_OF_IDR_PICTURE //||
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_A ||
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_B ||
        //nal_unit_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_C
      ) {

        AP4_DataBuffer unescaped(data, data_size);
        AP4_NalParser::Unescape(unescaped);
        AP4_BitReader bits(unescaped.GetData(), unescaped.GetDataSize());

        bits.SkipBits(8); // NAL Unit Type

        AP4_AvcFrameParser::ReadGolomb(bits); // first_mb_in_slice
        AP4_AvcFrameParser::ReadGolomb(bits); // slice_type
        pictureId = AP4_AvcFrameParser::ReadGolomb(bits); //picture_set_id
      }
      // move to the next NAL unit
      data += nalu_size;
      data_size -= nalu_size;
    }
  }

  virtual bool GetInformation(INPUTSTREAM_INFO &info) override
  {
    if (pictureId == pictureIdPrev)
      return false;
    pictureIdPrev = pictureId;

    if (AP4_AvcSampleDescription *avc = AP4_DYNAMIC_CAST(AP4_AvcSampleDescription, sample_description))
    {
      AP4_Array<AP4_DataBuffer>& ppsList(avc->GetPictureParameters());
      AP4_AvcPictureParameterSet pps;
      for (unsigned int i(0); i < ppsList.ItemCount(); ++i)
      {
        if (AP4_SUCCEEDED(AP4_AvcFrameParser::ParsePPS(ppsList[i].GetData(), ppsList[i].GetDataSize(), pps)) && pps.pic_parameter_set_id == pictureId)
        {
          AP4_Array<AP4_DataBuffer>& spsList = avc->GetSequenceParameters();
          AP4_AvcSequenceParameterSet sps;
          for (unsigned int i(0); i < spsList.ItemCount(); ++i)
          {
            if (AP4_SUCCEEDED(AP4_AvcFrameParser::ParseSPS(spsList[i].GetData(), spsList[i].GetDataSize(), sps)) && sps.seq_parameter_set_id == pps.seq_parameter_set_id)
            {
              bool ret = sps.GetInfo(info.m_Width, info.m_Height);
              ret = sps.GetVUIInfo(info.m_FpsRate, info.m_FpsScale, info.m_Aspect) || ret;
              return ret;
            }
          }
          break;
        }
      }
    }
    return false;
  };

  virtual STREAMCODEC_PROFILE GetProfile() override
  {
    return codecProfile;
  };
private:
  unsigned int countPictureSetIds;
  STREAMCODEC_PROFILE codecProfile;
  bool needSliceInfo;
};

/***********************   HEVC   ************************/

class HEVCCodecHandler : public CodecHandler
{
public:
  HEVCCodecHandler(AP4_SampleDescription *sd)
    :CodecHandler(sd)
  {
    if (AP4_HevcSampleDescription *hevc = AP4_DYNAMIC_CAST(AP4_HevcSampleDescription, sample_description))
    {
      extra_data.SetData(hevc->GetRawBytes().GetData(), hevc->GetRawBytes().GetDataSize());
      naluLengthSize = hevc->GetNaluLengthSize();
    }
  }
};

/***********************   MPEG   ************************/

class MPEGCodecHandler : public CodecHandler
{
public:
  MPEGCodecHandler(AP4_SampleDescription *sd)
    :CodecHandler(sd)
  {
    if (AP4_MpegSampleDescription *aac = AP4_DYNAMIC_CAST(AP4_MpegSampleDescription, sample_description))
      extra_data.SetData(aac->GetDecoderInfo().GetData(), aac->GetDecoderInfo().GetDataSize());
  }

  virtual bool GetInformation(INPUTSTREAM_INFO &info) override
  {
    AP4_AudioSampleDescription *asd;
    if (sample_description && (asd = AP4_DYNAMIC_CAST(AP4_AudioSampleDescription, sample_description)))
    {
      if (asd->GetChannelCount() != info.m_Channels
        || asd->GetSampleRate() != info.m_SampleRate
        || asd->GetSampleSize() != info.m_BitsPerSample)
      {
        info.m_Channels = asd->GetChannelCount();
        info.m_SampleRate = asd->GetSampleRate();
        info.m_BitsPerSample = asd->GetSampleSize();
        return true;
      }
    }
    return false;
  }
};

/*******************************************************
|   FragmentedSampleReader
********************************************************/
class FragmentedSampleReader : public SampleReader, public AP4_LinearReader
{
public:

  FragmentedSampleReader(AP4_ByteStream *input, AP4_Movie *movie, AP4_Track *track, AP4_UI32 streamId,
    AP4_CencSingleSampleDecrypter *ssd, const SSD::SSD_DECRYPTER::SSD_CAPS &dcaps)
    : AP4_LinearReader(*movie, input)
    , m_track(track)
    , m_streamId(streamId)
    , m_sampleDescIndex(1)
    , m_bSampleDescChanged(false)
    , m_decrypterCaps(dcaps)
    , m_failCount(0)
    , m_eos(false)
    , m_started(false)
    , m_dts(0)
    , m_pts(0)
    , m_ptsDiff(0)
    , m_ptsOffs(~0ULL)
    , m_codecHandler(0)
    , m_defaultKey(0)
    , m_protectedDesc(0)
    , m_singleSampleDecryptor(ssd)
    , m_decrypter(0)
    , m_nextDuration(0)
    , m_nextTimestamp(0)
  {
    EnableTrack(m_track->GetId());

    AP4_SampleDescription *desc(m_track->GetSampleDescription(0));
    if (desc->GetType() == AP4_SampleDescription::TYPE_PROTECTED)
    {
      m_protectedDesc = static_cast<AP4_ProtectedSampleDescription*>(desc);

      AP4_ContainerAtom *schi;
      if (m_protectedDesc->GetSchemeInfo() && (schi = m_protectedDesc->GetSchemeInfo()->GetSchiAtom()))
      {
        AP4_TencAtom* tenc(AP4_DYNAMIC_CAST(AP4_TencAtom, schi->GetChild(AP4_ATOM_TYPE_TENC, 0)));
        if (tenc)
          m_defaultKey = tenc->GetDefaultKid();
        else
        {
          AP4_PiffTrackEncryptionAtom* piff(AP4_DYNAMIC_CAST(AP4_PiffTrackEncryptionAtom, schi->GetChild(AP4_UUID_PIFF_TRACK_ENCRYPTION_ATOM, 0)));
          if (piff)
            m_defaultKey = piff->GetDefaultKid();
        }
      }
    }
    if (m_singleSampleDecryptor)
      m_poolId = m_singleSampleDecryptor->AddPool();

    m_timeBaseExt = DVD_TIME_BASE;
    m_timeBaseInt = m_track->GetMediaTimeScale();

    while (m_timeBaseExt > 1)
      if ((m_timeBaseInt / 10) * 10 == m_timeBaseInt)
      {
        m_timeBaseExt /= 10;
        m_timeBaseInt /= 10;
      }
      else
        break;

    //We need this to fill extradata
    UpdateSampleDescription();
  }

  ~FragmentedSampleReader()
  {
    if (m_singleSampleDecryptor)
      m_singleSampleDecryptor->RemovePool(m_poolId);
    delete m_decrypter;
    delete m_codecHandler;
  }

  virtual AP4_Result Start(bool &bStarted) override
  {
    bStarted = false;
    if (m_started)
      return AP4_SUCCESS;
    m_started = true;
    bStarted = true;
    return ReadSample();
  }

  virtual AP4_Result ReadSample() override;

  virtual void Reset(bool bEOS) override
  {
    AP4_LinearReader::Reset();
    m_eos = bEOS;
    if (m_codecHandler)
      m_codecHandler->Reset();
  }

  virtual bool EOS() const  override { return m_eos; };
  virtual uint64_t DTS()const override { return m_dts; };
  virtual uint64_t  PTS()const override { return m_pts; };

  virtual uint64_t  Elapsed(uint64_t basePTS) override
  {
    uint64_t manifestPTS = (m_pts > m_ptsDiff) ? m_pts - m_ptsDiff : 0;
    return manifestPTS > basePTS ? manifestPTS - basePTS : 0ULL;
  };

  virtual AP4_UI32 GetStreamId()const override { return m_streamId; };
  virtual AP4_Size GetSampleDataSize()const override { return m_sampleData.GetDataSize(); };
  virtual const AP4_Byte *GetSampleData()const override { return m_sampleData.GetData(); };
  virtual uint64_t GetDuration()const override { return (m_sample.GetDuration() * m_timeBaseExt) / m_timeBaseInt; };
  virtual bool IsEncrypted()const override { return (m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH) != 0 && m_decrypter != nullptr; };
  virtual bool GetInformation(INPUTSTREAM_INFO &info) override
  {
    if (!m_codecHandler)
      return false;

    bool edchanged(false);
    if (m_bSampleDescChanged && m_codecHandler->extra_data.GetDataSize()
      && (info.m_ExtraSize != m_codecHandler->extra_data.GetDataSize()
      || memcmp(info.m_ExtraData, m_codecHandler->extra_data.GetData(), info.m_ExtraSize)))
    {
      free((void*)(info.m_ExtraData));
      info.m_ExtraSize = m_codecHandler->extra_data.GetDataSize();
      info.m_ExtraData = (const uint8_t*)malloc(info.m_ExtraSize);
      memcpy((void*)info.m_ExtraData, m_codecHandler->extra_data.GetData(), info.m_ExtraSize);
      edchanged = true;
    }

    m_bSampleDescChanged = false;

    if (m_codecHandler->GetInformation(info))
      return true;

    return edchanged;
  }

  virtual bool TimeSeek(uint64_t  pts, bool preceeding) override
  {
    AP4_Ordinal sampleIndex;
    AP4_UI64 seekPos(static_cast<AP4_UI64>(((pts + m_ptsDiff) * m_timeBaseInt) / m_timeBaseExt));
    if (AP4_SUCCEEDED(SeekSample(m_track->GetId(), seekPos, sampleIndex, preceeding)))
    {
      if (m_decrypter)
        m_decrypter->SetSampleIndex(sampleIndex);
      if (m_codecHandler)
        m_codecHandler->TimeSeek(seekPos);
      m_started = true;
      return AP4_SUCCEEDED(ReadSample());
    }
    return false;
  };

  virtual void SetPTSOffset(uint64_t offset) override
  {
    FindTracker(m_track->GetId())->m_NextDts = (offset * m_timeBaseInt) / m_timeBaseExt;
    m_ptsOffs = offset;
    if (m_codecHandler)
      m_codecHandler->SetPTSOffset((offset * m_timeBaseInt) / m_timeBaseExt);
  };

  virtual bool GetNextFragmentInfo(uint64_t &ts, uint64_t &dur) override
  {
    if (m_nextDuration)
    {
      dur = m_nextDuration;
      ts = m_nextTimestamp;
    }
    else
    {
      dur = dynamic_cast<AP4_FragmentSampleTable*>(FindTracker(m_track->GetId())->m_SampleTable)->GetDuration();
      ts = 0;
    }
    return true;
  };
  virtual uint32_t GetTimeScale()const override { return m_track->GetMediaTimeScale(); };

protected:
  virtual AP4_Result ProcessMoof(AP4_ContainerAtom* moof,
    AP4_Position       moof_offset,
    AP4_Position       mdat_payload_offset,
    AP4_UI64 mdat_payload_size) override
  {
    AP4_Result result;

    if (AP4_SUCCEEDED((result = AP4_LinearReader::ProcessMoof(moof, moof_offset, mdat_payload_offset, mdat_payload_size))))
    {
      AP4_ContainerAtom *traf = AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->GetChild(AP4_ATOM_TYPE_TRAF, 0));

      //For ISM Livestreams we have an UUID atom with one / more following fragment durations
      m_nextDuration = m_nextTimestamp = 0;
      AP4_Atom *atom;
      unsigned int atom_pos(0);
      const uint8_t uuid[16] = { 0xd4, 0x80, 0x7e, 0xf2, 0xca, 0x39, 0x46, 0x95, 0x8e, 0x54, 0x26, 0xcb, 0x9e, 0x46, 0xa7, 0x9f };
      while ((atom = traf->GetChild(AP4_ATOM_TYPE_UUID, atom_pos++))!= nullptr)
      {
        AP4_UuidAtom *uuid_atom(AP4_DYNAMIC_CAST(AP4_UuidAtom, atom));
        if (memcmp(uuid_atom->GetUuid(), uuid, 16) == 0)
        {
          //verison(8) + flags(24) + numpairs(8) + pairs(ts(64)/dur(64))*numpairs
          const AP4_DataBuffer &buf(AP4_DYNAMIC_CAST(AP4_UnknownUuidAtom, uuid_atom)->GetData());
          if (buf.GetDataSize() >= 21)
          {
            const uint8_t *data(buf.GetData());
            m_nextTimestamp = AP4_BytesToUInt64BE(data + 5);
            m_nextDuration = AP4_BytesToUInt64BE(data + 13);
          }
          break;
        }
      }

      //Check if the sample table description has changed
      AP4_TfhdAtom *tfhd = AP4_DYNAMIC_CAST(AP4_TfhdAtom, traf->GetChild(AP4_ATOM_TYPE_TFHD, 0));
      if ((tfhd && tfhd->GetSampleDescriptionIndex() != m_sampleDescIndex) || (!tfhd && (m_sampleDescIndex = 1)))
      {
        m_sampleDescIndex = tfhd->GetSampleDescriptionIndex();
        UpdateSampleDescription();
      }

      //Correct PTS
      AP4_Sample sample;
      if (~m_ptsOffs)
      {
        if (AP4_SUCCEEDED(GetSample(m_track->GetId(), sample, 0)))
        {
          m_pts = m_dts = (sample.GetCts() * m_timeBaseExt) / m_timeBaseInt;
          m_ptsDiff = m_pts - m_ptsOffs;
        }
        m_ptsOffs = ~0ULL;
      }

      if (m_protectedDesc)
      {
        //Setup the decryption
        AP4_CencSampleInfoTable *sample_table;
        AP4_UI32 algorithm_id = 0;

        delete m_decrypter;
        m_decrypter = 0;

        AP4_ContainerAtom *traf = AP4_DYNAMIC_CAST(AP4_ContainerAtom, moof->GetChild(AP4_ATOM_TYPE_TRAF, 0));

        if (!m_protectedDesc || !traf)
          return AP4_ERROR_INVALID_FORMAT;

        if (AP4_FAILED(result = AP4_CencSampleInfoTable::Create(m_protectedDesc, traf, algorithm_id, *m_FragmentStream, moof_offset, sample_table)))
          // we assume unencrypted fragment here
          goto SUCCESS;

        if (AP4_FAILED(result = AP4_CencSampleDecrypter::Create(sample_table, algorithm_id, 0, 0, 0, m_singleSampleDecryptor, m_decrypter)))
          return result;
      }
    }
SUCCESS:
    if (m_singleSampleDecryptor && m_codecHandler)
      m_singleSampleDecryptor->SetFragmentInfo(m_poolId, m_defaultKey, m_codecHandler->naluLengthSize, m_codecHandler->extra_data, m_decrypterCaps.flags);

    return AP4_SUCCESS;
  }

private:

  void UpdateSampleDescription()
  {
    if (m_codecHandler)
      delete m_codecHandler;
    m_codecHandler = 0;
    m_bSampleDescChanged = true;

    AP4_SampleDescription *desc(m_track->GetSampleDescription(m_sampleDescIndex - 1));
    if (desc->GetType() == AP4_SampleDescription::TYPE_PROTECTED)
    {
      m_protectedDesc = static_cast<AP4_ProtectedSampleDescription*>(desc);
      desc = m_protectedDesc->GetOriginalSampleDescription();
    }
    switch (desc->GetFormat())
    {
    case AP4_SAMPLE_FORMAT_AVC1:
    case AP4_SAMPLE_FORMAT_AVC2:
    case AP4_SAMPLE_FORMAT_AVC3:
    case AP4_SAMPLE_FORMAT_AVC4:
      m_codecHandler = new AVCCodecHandler(desc);
      break;
    case AP4_SAMPLE_FORMAT_HEV1:
    case AP4_SAMPLE_FORMAT_HVC1:
      m_codecHandler = new HEVCCodecHandler(desc);
      break;
    case AP4_SAMPLE_FORMAT_MP4A:
      m_codecHandler = new MPEGCodecHandler(desc);
      break;
    case AP4_SAMPLE_FORMAT_STPP:
//      m_codecHandler = new TTMLCodecHandler(desc);
      throw;
      break;
    default:
      m_codecHandler = new CodecHandler(desc);
      break;
    }

    if ((m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_ANNEXB_REQUIRED) != 0)
      m_codecHandler->ExtraDataToAnnexB();
  }

private:
  AP4_Track *m_track;
  AP4_UI32 m_streamId;
  AP4_UI32 m_sampleDescIndex;
  bool m_bSampleDescChanged;
  SSD::SSD_DECRYPTER::SSD_CAPS m_decrypterCaps;
  unsigned int m_failCount;
  AP4_UI32 m_poolId;

  bool m_eos, m_started;
  int64_t m_dts, m_pts, m_ptsDiff;
  AP4_UI64 m_ptsOffs;

  uint64_t m_timeBaseExt, m_timeBaseInt;

  AP4_Sample     m_sample;
  AP4_DataBuffer m_encrypted, m_sampleData;

  CodecHandler *m_codecHandler;
  const AP4_UI08 *m_defaultKey;

  AP4_ProtectedSampleDescription *m_protectedDesc;
  AP4_CencSingleSampleDecrypter *m_singleSampleDecryptor;
  AP4_CencSampleDecrypter *m_decrypter;
  uint64_t m_nextDuration, m_nextTimestamp;
};
