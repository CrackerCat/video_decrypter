#include "main.h"

#ifndef OS_WIN
#include <sys/stat.h>
#include <unistd.h>
#endif


AP4_Result FragmentedSampleReader::ReadSample()
{
    AP4_Result result;
    if (!m_codecHandler || !m_codecHandler->ReadNextSample(m_sample, m_sampleData))
    {
        bool useDecryptingDecoder = m_protectedDesc && (m_decrypterCaps.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH) != 0;
        bool decrypterPresent(m_decrypter != nullptr);

        if (AP4_FAILED(result = ReadNextSample(m_track->GetId(), m_sample, (m_decrypter || useDecryptingDecoder) ? m_encrypted : m_sampleData)))
        {
            if (result == AP4_ERROR_EOS)
            {
                if (dynamic_cast<AP4_DASHStream*>(m_FragmentStream)->waitingForSegment())
                    m_sampleData.SetDataSize(0);
                else
                    m_eos = true;
            }
            return result;
        }

        //Protection could have changed in ProcessMoof
        if (!decrypterPresent && m_decrypter != nullptr && !useDecryptingDecoder)
            m_encrypted.SetData(m_sampleData.GetData(), m_sampleData.GetDataSize());
        else if (decrypterPresent && m_decrypter == nullptr && !useDecryptingDecoder)
            m_sampleData.SetData(m_encrypted.GetData(), m_encrypted.GetDataSize());

        // Make sure that the decrypter is NOT allocating memory!
        // If decrypter and addon are compiled with different DEBUG / RELEASE
        // options freeing HEAP memory will fail.
        if (m_decrypter)
        {
            m_sampleData.Reserve(m_encrypted.GetDataSize() + 4096);
            result = m_decrypter->DecryptSampleData(m_poolId, m_encrypted, m_sampleData, NULL);
        }
        else if (useDecryptingDecoder)
        {
            m_sampleData.Reserve(m_encrypted.GetDataSize() + 1024);
            result = m_singleSampleDecryptor->DecryptSampleData(m_poolId, m_encrypted, m_sampleData, nullptr, 0, nullptr, nullptr);
        }
    }

    if(IsEncrypted())
    {
        printf("Decryption of track not yet supported.\n");
        exit(-1);
    }

    // Write initialisation & data into decrypted file
    AP4_Position pos_after_sample;
    m_FragmentStream->Tell(pos_after_sample);
    pos_after_sample -= stream_start_pos;

    int pos_decrypted = file_decrypted_data.tellp();
    int metadata_length = pos_after_sample - m_sampleData.GetDataSize() - pos_decrypted;

    std::vector<char> buffer(metadata_length, 0);
    file_fragment.seekg(pos_decrypted, std::ios::beg);
    file_fragment.read(buffer.data(), metadata_length);
    file_decrypted_data.write(buffer.data(), metadata_length);
    file_decrypted_data.write((const char*)m_sampleData.GetData(), m_sampleData.GetDataSize());

    return result;
};

class MyHost : public SSD::SSD_HOST
{
public:
    virtual const char *GetLibraryPath() override
    {
        return CDM_PATH;
    }
    virtual const char *GetProfilePath() override
    {
        return profile_path.c_str();
    }
    virtual void* CURLCreate(const char* strURL) override
    {
        std::stringstream *ss = new std::stringstream();
        *ss << "curl -k " << strURL;
        return ss;
    }
    virtual bool CURLAddOption(void* file, CURLOPTIONS opt, const char* name, const char * value) override
    {
        if(std::string(name).compare("postdata") == 0)
            *(std::stringstream*)file << " --data-binary @\"" << GetProfilePath() << "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.challenge\" ";
        else
            *(std::stringstream*)file << " -H \"" << name << ": " << value << "\" ";
        return true;
    }
    virtual bool CURLOpen(void* file) override
    {
        std::stringstream *ss = (std::stringstream*)file;
        *ss << "-o \"" << GetProfilePath() << "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.response\"";
#ifdef OS_WIN
        return StartProcess((*ss).str().c_str());
#endif
        return execl((*ss).str().c_str(), NULL);
    }
    virtual std::vector<char> ReadFile() override
    {
        std::ifstream file_response(std::string(GetProfilePath()) + "EDEF8BA9-79D6-4ACE-A3C8-27DCD51D21ED.response", std::ios::binary);
        file_response.seekg(0, std::ios::end);
        int length = file_response.tellg();
        file_response.seekg(0, std::ios::beg);

        std::vector<char> buffer(length, 0);
        file_response.read(buffer.data(), length);
        file_response.close();
        return buffer;
    }
    virtual void CloseFile(void* file) override
    {
        ((std::stringstream*)file)->str("");
        file=nullptr;
    }
    virtual bool Create_Directory(const char *dir) override
    {
#ifdef OS_WIN
        return !CreateDirectoryA(dir, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
        return mkdir(dir, 0755) == 0;
#endif
    }
    virtual void Log(LOGLEVEL level, const char *msg) override
    {
        printf(msg);
        printf("\n");
    }
    virtual bool GetBuffer(void* instance, SSD::SSD_PICTURE &picture) override
    {
        throw;
    }
    virtual void ReleaseBuffer(void* instance, void *buffer) override
    {
        throw;
    }
};

bool MyAdaptiveStream::download(const char* url, const std::map<std::string, std::string> &mediaHeaders)
{
    file_fragment.seekg(0, std::ios::end);
    int length = file_fragment.tellg();
    file_fragment.seekg(0, std::ios::beg);

    std::vector<char> buffer(length, 0);
    file_fragment.read(buffer.data(), length);
    write_data(buffer.data(), length);

    file_fragment.clear();
    return true;
}

bool adaptive::AdaptiveTree::download(const char* url, const std::map<std::string, std::string> &manifestHeaders, void *opaque)
{
    file_mpd = std::ifstream(info_path+"\\manifest.mpd");
    std::stringstream buffer;
    buffer << file_mpd.rdbuf();
    file_mpd.close();
    write_data((void*)buffer.str().c_str(), buffer.str().size(), opaque);
    return true;
}


int main(int argc, char *argv[])
{
    if(argc != 5)
    {
        printf("Syntax : %s {encrypted_file} {stream_id} {info_path} {decrypted_path}\n", argv[0]);
        return -1;
    }

    encrypted_file = argv[1];
    stream_id_str = argv[2];
    info_path = argv[3];
    decrypted_path = argv[4];

    std::string nom_fragment = encrypted_file.substr(encrypted_file.find_last_of("/\\")+1);
    std::string type_fragment = nom_fragment.substr(nom_fragment.find("_")+1);

    int stream_id;
    try
    {
        stream_id = std::stoi(stream_id_str, nullptr);
    }
    catch(const std::exception& e)
    {
        printf("Unknown stream id.\n");
        return -1;
    }

    std::string nom_video = nom_fragment.substr(0,nom_fragment.find("_"));
    printf("File : %s\n", nom_fragment.c_str());
    printf("Info path : %s\n", info_path.c_str());
    printf("Decrypted path : %s\n", decrypted_path.c_str());

    file_fragment = std::ifstream(encrypted_file, std::ios::binary);
    file_decrypted_data = std::ofstream(decrypted_path+"\\"+nom_video+"_track_"+stream_id_str, std::ios::binary | std::ios::trunc);

    MyHost host;
    profile_path = info_path+"\\"+std::to_string(std::time(0))+"\\";
    host.Create_Directory(profile_path.c_str());

    // --------------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------------
    printf("Initialisation...\n");

    std::ifstream file_lic(info_path+"\\license_key.txt");
    std::stringstream buffer;
    buffer << file_lic.rdbuf();
    file_lic.close();

    std::string license_key_ = buffer.str();
    std::string license_data_;
    std::string license_type_ = "com.widevine.alpha";

    typedef SSD::SSD_DECRYPTER *(*CreateDecryptorInstanceFunc)(SSD::SSD_HOST *host, uint32_t version);

    adaptiveTree_ = new adaptive::DASHTree;
    adaptiveTree_->bandwidth_ = 100000000;
    SSD::SSD_DECRYPTER *decrypter_;

#ifdef NDEBUG
    std::string lib_wvdecrypter_path = std::string(dirname(strdup(argv[0]))) + "/wvdecrypter/libssd_wv.dll";
#else
    std::string lib_wvdecrypter_path = std::string(dirname(strdup(argv[0]))) + "/wvdecrypter/libssd_wvd.dll";
#endif // NDEBUG

    void * mod(dlopen(lib_wvdecrypter_path.c_str(), RTLD_LAZY));
    if (mod)
    {
        CreateDecryptorInstanceFunc startup;
        if ((startup = (CreateDecryptorInstanceFunc)dlsym(mod, "CreateDecryptorInstance")))
        {
            SSD::SSD_DECRYPTER *decrypter = startup(&host, SSD::SSD_HOST::version);
            const char *suppUrn(0);

            if (decrypter && (suppUrn = decrypter->SelectKeySytem(license_type_.c_str())))
            {
                decrypter_ = decrypter;
                adaptiveTree_->supportedKeySystem_ = suppUrn;
            }
        }
    }
    else
    {
        printf("%s\n", dlerror());
        return -1;
    }

    if (!adaptiveTree_->open("", "") || adaptiveTree_->empty())
    {
        printf("Could not open / parse mpd manifest.\n");
        return -1;
    }

    printf("Successfully parsed .mpd file. #Streams: %d\n", adaptiveTree_->periods_[0]->adaptationSets_.size());

    if (adaptiveTree_->encryptionState_ == adaptive::AdaptiveTree::ENCRYTIONSTATE_ENCRYPTED)
    {
        printf("Unable to handle decryption. Unsupported!\n");
        return -1;
    }

    unsigned int i(0);
    const adaptive::AdaptiveTree::AdaptationSet *adp;

    bool secure_video_session_;
    cdm_sessions_.resize(adaptiveTree_->psshSets_.size());
    memset(&cdm_sessions_.front(), 0, sizeof(CDMSESSION));

    if (adaptiveTree_->encryptionState_)
    {
        printf("Entering encryption section\n");

        if (license_key_.empty())
        {
            printf("Invalid license_key\n");
            return -1;
        }
        if (!decrypter_)
        {
            printf("No decrypter found for encrypted stream\n");
            return -1;
        }
        if(!decrypter_->OpenDRMSystem(license_key_.c_str(), AP4_DataBuffer()))
        {
            printf("OpenDRMSystem failed");
            return -1;
        }

        AP4_DataBuffer init_data;
        const char *optionalKeyParameter(nullptr);

        for (size_t ses(1); ses < cdm_sessions_.size(); ++ses)
        {
            if (adaptiveTree_->psshSets_[ses].pssh_ == "FILE")
            {
                if (license_data_.empty())
                {
                    uint16_t width_=8192, height_=8192;

                    std::string strkey(adaptiveTree_->supportedKeySystem_.substr(9));
                    size_t pos;
                    while ((pos = strkey.find('-')) != std::string::npos)
                        strkey.erase(pos, 1);
                    if (strkey.size() != 32)
                    {
                        printf("Key system mismatch (%s)!\n", adaptiveTree_->supportedKeySystem_.c_str());
                        return -1;
                    }

                    unsigned char key_system[16];
                    AP4_ParseHex(strkey.c_str(), key_system, 16);

                    STREAM stream(*adaptiveTree_, adaptiveTree_->GetAdaptationSet(0)->type_);
                    stream.stream_.prepare_stream(adaptiveTree_->GetAdaptationSet(0), 0, 0, 0, 0, 0, 0, 0, std::map<std::string, std::string>());

                    stream.enabled = true;
                    stream.stream_.start_stream(0, width_, height_);
                    stream.stream_.select_stream(true, false, stream.info_.m_pID >> 16);

                    stream.input_ = new AP4_DASHStream(&stream.stream_);
                    stream.input_file_ = new AP4_File(*stream.input_, AP4_DefaultAtomFactory::Instance, true);
                    AP4_Movie* movie = stream.input_file_->GetMovie();
                    if (movie == NULL)
                    {
                        printf("No MOOV in stream!\n");
                        stream.disable();
                        return -1;
                    }
                    AP4_Array<AP4_PsshAtom*>& pssh = movie->GetPsshAtoms();

                    for (unsigned int i = 0; !init_data.GetDataSize() && i < pssh.ItemCount(); i++)
                    {
                        if (memcmp(pssh[i]->GetSystemId(), key_system, 16) == 0)
                        {
                            init_data.AppendData(pssh[i]->GetData().GetData(), pssh[i]->GetData().GetDataSize());
                            if (adaptiveTree_->psshSets_[ses].defaultKID_.empty())
                            {
                                if (pssh[i]->GetKid(0))
                                    adaptiveTree_->psshSets_[ses].defaultKID_ = std::string((const char*)pssh[i]->GetKid(0), 16);
                                else if (AP4_Track *track = movie->GetTrack(TIDC[stream.stream_.get_type()]))
                                {
                                    AP4_ProtectedSampleDescription *m_protectedDesc = static_cast<AP4_ProtectedSampleDescription*>(track->GetSampleDescription(0));
                                    AP4_ContainerAtom *schi;
                                    if (m_protectedDesc->GetSchemeInfo() && (schi = m_protectedDesc->GetSchemeInfo()->GetSchiAtom()))
                                    {
                                        AP4_TencAtom* tenc(AP4_DYNAMIC_CAST(AP4_TencAtom, schi->GetChild(AP4_ATOM_TYPE_TENC, 0)));
                                        if (tenc)
                                            adaptiveTree_->psshSets_[ses].defaultKID_ = std::string((const char*)tenc->GetDefaultKid());
                                        else
                                        {
                                            AP4_PiffTrackEncryptionAtom* piff(AP4_DYNAMIC_CAST(AP4_PiffTrackEncryptionAtom, schi->GetChild(AP4_UUID_PIFF_TRACK_ENCRYPTION_ATOM, 0)));
                                            if (piff)
                                                adaptiveTree_->psshSets_[ses].defaultKID_ = std::string((const char*)piff->GetDefaultKid());
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (!init_data.GetDataSize())
                    {
                        printf("Could not extract license from video stream (PSSH not found)\n");
                        stream.disable();
                        return -1;
                    }
                    stream.disable();
                }
                else if (!adaptiveTree_->psshSets_[ses].defaultKID_.empty())
                {
                    init_data.SetData((AP4_Byte*)adaptiveTree_->psshSets_[ses].defaultKID_.data(), 16);

                    uint8_t ld[1024];
                    unsigned int ld_size(1014);
                    b64_decode(license_data_.c_str(), license_data_.size(), ld, ld_size);

                    uint8_t *uuid((uint8_t*)strstr((const char*)ld, "{KID}"));
                    if (uuid)
                    {
                        memmove(uuid + 11, uuid, ld_size - (uuid - ld));
                        memcpy(uuid, init_data.GetData(), init_data.GetDataSize());
                        init_data.SetData(ld, ld_size + 11);
                    }
                    else
                        init_data.SetData(ld, ld_size);
                }
                else
                    return -1;
            }
            else
            {
                init_data.SetBufferSize(1024);
                unsigned int init_data_size(1024);
                b64_decode(adaptiveTree_->psshSets_[ses].pssh_.data(), adaptiveTree_->psshSets_[ses].pssh_.size(), init_data.UseData(), init_data_size);
                init_data.SetDataSize(init_data_size);
            }

            CDMSESSION &session(cdm_sessions_[ses]);
            const char *defkid = adaptiveTree_->psshSets_[ses].defaultKID_.empty() ? nullptr : adaptiveTree_->psshSets_[ses].defaultKID_.data();
            session.single_sample_decryptor_ = nullptr;
            session.shared_single_sample_decryptor_ = false;

            if (decrypter_ && defkid)
            {
                for (unsigned int i(1); i < ses; ++i)
                    if (decrypter_ && decrypter_->HasLicenseKey(cdm_sessions_[i].single_sample_decryptor_, (const uint8_t *)defkid))
                    {
                        session.single_sample_decryptor_ = cdm_sessions_[i].single_sample_decryptor_;
                        session.shared_single_sample_decryptor_ = true;
                    }
            }

            if (decrypter_ && init_data.GetDataSize() >= 4 && (session.single_sample_decryptor_
                    || (session.single_sample_decryptor_ = decrypter_->CreateSingleSampleDecrypter(init_data, optionalKeyParameter)) != 0))
            {

                decrypter_->GetCapabilities(
                    session.single_sample_decryptor_,
                    (const uint8_t *)defkid,
                    adaptiveTree_->psshSets_[ses].media_,
                    session.decrypter_caps_);

                if (session.decrypter_caps_.flags & SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_PATH)
                {
                    session.cdm_session_str_ = session.single_sample_decryptor_->GetSessionId();
                    secure_video_session_ = true;
                    // Override this setting by information passed in manifest
                    if (!adaptiveTree_->need_secure_decoder_)
                        session.decrypter_caps_.flags &= ~SSD::SSD_DECRYPTER::SSD_CAPS::SSD_SECURE_DECODER;
                }
            }
            else
            {
                printf("Initialize failed (SingleSampleDecrypter)\n");
                for (unsigned int i(ses); i < cdm_sessions_.size(); ++i)
                    cdm_sessions_[i].single_sample_decryptor_ = nullptr;
                return -1;
            }
        }
    }

    bool manual_streams_ = false;
    uint32_t min_bandwidth(0), max_bandwidth(0);
    const std::map<std::string, std::string>  media_headers_= {{"User-Agent", "Mozilla/5.0"}};

    while ((adp = adaptiveTree_->GetAdaptationSet(i++)))
    {
        size_t repId = manual_streams_ ? adp->representations_.size() : 0;

        do
        {
            streams_.push_back(new STREAM(*adaptiveTree_, adp->type_));
            STREAM &stream(*streams_.back());
            const SSD::SSD_DECRYPTER::SSD_CAPS &caps(GetDecrypterCaps(adp->representations_[0]->get_psshset()));

            uint32_t hdcpLimit = 0;
            uint16_t hdcpVersion = 99;

            stream.stream_.prepare_stream(adp, GetVideoWidth(), GetVideoHeight(), hdcpLimit, hdcpVersion, min_bandwidth, max_bandwidth, repId, media_headers_);
            stream.info_.m_flags = INPUTSTREAM_INFO::FLAG_NONE;

            switch (adp->type_)
            {
            case adaptive::AdaptiveTree::VIDEO:
                stream.info_.m_streamType = INPUTSTREAM_INFO::TYPE_VIDEO;
                break;
            case adaptive::AdaptiveTree::AUDIO:
                stream.info_.m_streamType = INPUTSTREAM_INFO::TYPE_AUDIO;
                if (adp->impaired_)
                    stream.info_.m_flags |= INPUTSTREAM_INFO::FLAG_VISUAL_IMPAIRED;
                break;
            default:
                break;
            }
            stream.info_.m_pID = i | (repId << 16);
            strcpy(stream.info_.m_language, adp->language_.c_str());
            stream.info_.m_ExtraData = nullptr;
            stream.info_.m_ExtraSize = 0;
            stream.info_.m_features = 0;
            stream.stream_.set_observer(new adaptive::AdaptiveStreamObserver());

            UpdateStream(stream, caps);
        }
        while (repId-- != (manual_streams_? 1 : 0));
    }

    // --------------------------------------------------------------------------------------------------------------
    // --------------------------------------------------------------------------------------------------------------
    printf("OpenStream(%d)\n", stream_id);

    STREAM *stream(GetStream(stream_id));

    if (!stream || stream->enabled)
    {
        printf("Stream id not found.\n");
        return -1;
    }

    stream->enabled = true;

    stream->stream_.start_stream(~0, GetVideoWidth(), GetVideoHeight());
	stream_start_pos = stream->stream_.tell();

    const adaptive::AdaptiveTree::Representation *rep(stream->stream_.getRepresentation());

    // If we select a dummy (=inside video) stream, open the video part
    // Dummy streams will be never enabled, they will only enable / activate audio track.
    if (rep->flags_ & adaptive::AdaptiveTree::Representation::INCLUDEDSTREAM)
    {
        STREAM *mainStream;
        stream->mainId_ = 0;
        while ((mainStream = GetStream(++stream->mainId_)))
            if (mainStream->info_.m_streamType == INPUTSTREAM_INFO::TYPE_VIDEO && mainStream->enabled)
                break;
        if (mainStream)
        {
            mainStream->reader_->AddStreamType(stream->info_.m_streamType, stream_id);
            mainStream->reader_->GetInformation(stream->info_);
        }
        else
            stream->mainId_ = 0;
        m_IncludedStreams[stream->info_.m_streamType] = stream_id;
        return -1;
    }

    printf("Selecting stream with conditions: w: %u, h: %u, bw: %u\n",
           stream->stream_.getWidth(), stream->stream_.getHeight(), stream->stream_.getBandwidth());

    if (!stream->stream_.select_stream(true, false, stream->info_.m_pID >> 16))
    {
        printf("Unable to select stream!\n");
        stream->disable();
        return -1;
    }

    if (rep != stream->stream_.getRepresentation())
    {
        UpdateStream(*stream, GetDecrypterCaps(stream->stream_.getRepresentation()->pssh_set_));
        CheckChange(true);
    }

    AP4_Movie* movie(PrepareStream(stream));
    stream->input_ = new AP4_DASHStream(&stream->stream_);
    stream->input_file_ = new AP4_File(*stream->input_, AP4_DefaultAtomFactory::Instance, true, movie);
    movie = stream->input_file_->GetMovie();

    if (movie == NULL)
    {
        printf("No MOOV in stream!\n");
        stream->disable();
        return -1;
    }

    AP4_Track *track = movie->GetTrack(TIDC[stream->stream_.get_type()]);
    if (!track)
    {
        printf("No suitable track found in stream\n");
        stream->disable();
        return -1;
    }

    stream->reader_ = new FragmentedSampleReader(stream->input_, movie, track, stream_id,
            GetSingleSampleDecryptor(stream->stream_.getRepresentation()->pssh_set_),
            GetDecrypterCaps(stream->stream_.getRepresentation()->pssh_set_));

    if (stream->info_.m_streamType == INPUTSTREAM_INFO::TYPE_VIDEO)
    {
        for (uint16_t i(0); i < 16; ++i)
            if (m_IncludedStreams[i])
            {
                stream->reader_->AddStreamType(static_cast<INPUTSTREAM_INFO::STREAM_TYPE>(i), m_IncludedStreams[i]);
                stream->reader_->GetInformation(GetStream(m_IncludedStreams[i])->info_);
            }
    }
    stream->reader_->GetInformation(stream->info_);
    SampleReader *sr(GetNextSample());

    AP4_Result result = AP4_SUCCESS;
    int error_count = 0;
    int sample_count = 0;
    while (error_count < 50 && result != AP4_ERROR_EOS)
    {
        // Limit of 1000 samples/second for CDM decryption
        if(sample_count>900)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            sample_count = 0;
        }

        result = sr->ReadSample();
        sample_count++;

        if(AP4_SUCCEEDED(result))
            error_count = 0;
        else
            error_count++;
    }

    printf("Decryption finished.\n");
    return 0;
}
