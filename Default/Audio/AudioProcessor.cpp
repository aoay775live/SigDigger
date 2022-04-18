//
//    AudioProcessor.cpp: Audio processor
//    Copyright (C) 2022 Gonzalo José Carracedo Carballal
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as
//    published by the Free Software Foundation, either version 3 of the
//    License, or (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful, but
//    WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public
//    License along with this program.  If not, see
//    <http://www.gnu.org/licenses/>
//
#include "AudioProcessor.h"
#include "AudioPlayback.h"

#include <AppConfig.h>
#include <SuWidgetsHelpers.h>
#include <Suscan/AnalyzerRequestTracker.h>

using namespace SigDigger;

AudioProcessor::AudioProcessor(UIMediator *, QObject *parent)
  : QObject(parent)
{
  try {
    m_playBack = new AudioPlayback("default", m_sampleRate);
    m_tracker = new Suscan::AnalyzerRequestTracker(this);

    // Connects the tracker
    this->connectAll();
  } catch (std::runtime_error &) {
    m_playBack = nullptr;
  }
}

void
AudioProcessor::connectAll()
{
  connect(
        this->m_tracker,
        SIGNAL(opened(AnalyzerRequest const &req)),
        this,
        SLOT(onOpened(Suscan::AnalyzerRequest const &)));

  connect(
        this->m_tracker,
        SIGNAL(cancelled(AnalyzerRequest const &req)),
        this,
        SLOT(onCancelled(Suscan::AnalyzerRequest const &)));

  connect(
        this->m_tracker,
        SIGNAL(error(AnalyzerRequest const &req, const std::string &)),
        this,
        SLOT(onError(Suscan::AnalyzerRequest const &, , const std::string &)));
}

bool
AudioProcessor::openAudio()
{
  // Opening audio is a multi-step, asynchronous process, that involves:
  // 1. Performing the request through the request tracker
  // 2. Signaling the completion of the request
  // 3. Setting channel properties asynchronously and waiting for its
  //    completion
  // 4. Signal audio open back to the user

  bool opening = false;

  assert(m_analyzer != nullptr);

  if (m_opening)
    return true;

  if (!m_opened) {
    if (m_playBack != nullptr) {
      Suscan::Channel ch;
      SUFREQ maxFc = SCAST(SUFREQ, m_analyzer->getSampleRate() / 2);
      SUFREQ bw  = SIGDIGGER_AUDIO_INSPECTOR_BANDWIDTH;
      unsigned int reqRate = m_sampleRate;

      // FIXME: Find a sample rate that better matches this
      if (reqRate > bw)
        reqRate = SCAST(unsigned int, floor(bw));

      // Configure sample rate
      m_playBack->setVolume(m_volume);
      m_playBack->setSampleRate(reqRate);
      m_playBack->start();

      // TODO: Recover true sample rate?
      m_sampleRate = m_playBack->getSampleRate();

      if (bw > maxFc)
        bw = maxFc;

      // Prepare channel
      ch.bw    = bw;
      ch.ft    = 0;
      ch.fc    = m_lo;
      ch.fLow  = -.5 * bw;
      ch.fHigh = +.5 * bw;

      if (ch.fc > maxFc || ch.fc < -maxFc)
        ch.fc = 0;

      // Async step 1: track request
      opening = m_tracker->requestOpen("audio", ch);

      if (!opening) {
        emit audioError("Internal Suscan error while opening audio inspector");
        m_playBack->stop();
      }
    } else {
      emit audioError("Cannot enable audio, playback support failed to start");
    }

    m_opening = opening;
  }

  return opening;
}

bool
AudioProcessor::closeAudio()
{
  assert(m_analyzer != nullptr);

  if (m_opening || m_opened) {
    // Inspector opened: close it
    if (m_audioInspectorOpened)
      m_analyzer->closeInspector(m_audioInspHandle);

    if (!m_opened)
      m_tracker->cancelAll();

    m_playBack->stop();
  }

  m_opening = false;
  m_opened  = false;
  m_audioInspectorOpened = false;
}

void
AudioProcessor::setParams()
{
  assert(m_audioCfgTemplate != nullptr);
  assert(m_analyzer != nullptr);
  assert(m_audioInspectorOpened);

  Suscan::Config cfg(m_audioCfgTemplate);
  cfg.set("audio.cutoff", m_cutOff);
  cfg.set("audio.volume", 1.f); // We handle this at UI level
  cfg.set("audio.sample-rate", SCAST(uint64_t, m_sampleRate));
  cfg.set("audio.demodulator", SCAST(uint64_t, m_sampleRate));
  cfg.set("audio.squelch", m_squelch);
  cfg.set("audio.squelch-level", m_squelchLevel);

  // Set audio inspector parameters
  m_analyzer->setInspectorConfig(m_audioInspHandle, cfg);
}

void
AudioProcessor::disconnectAnalyzer()
{
  disconnect(m_analyzer, nullptr, this, nullptr);
}

void
AudioProcessor::connectAnalyzer()
{
  connect(
        m_analyzer,
        SIGNAL(inspector_message(const Suscan::InspectorMessage &)),
        this,
        SLOT(onInspectorMessage(const Suscan::InspectorMessage &)));
}

void
AudioProcessor::setAnalyzer(Suscan::Analyzer *analyzer)
{
  if (m_analyzer != nullptr) {
    this->disconnectAnalyzer();
    this->closeAudio();
  }

  m_analyzer = analyzer;
  m_tracker->setAnalyzer(analyzer);

  // Was audio enabled? Open it back
  if (m_analyzer != nullptr)
    if (m_enabled)
      this->openAudio();
}

void
AudioProcessor::setEnabled(bool enabled)
{
  if (m_enabled != enabled) {
    m_enabled = enabled;

    if (enabled) {
      if (!m_opened && !m_opening)
        this->openAudio();
    } else {
      if (m_opened || m_opening)
        this->closeAudio();
    }
  }
}

void
AudioProcessor::setSquelchEnabled(bool enabled)
{
  if (m_squelch != enabled) {
    m_squelch = enabled;

    if (m_audioInspectorOpened)
      this->setParams();
  }
}

void
AudioProcessor::setSquelchLevel(float level)
{
  if (!sufeq(m_squelchLevel, level, 1e-8f)) {
    m_squelchLevel = level;

    if (m_audioInspectorOpened)
      this->setParams();
  }
}

void
AudioProcessor::setVolume(float volume)
{
  if (!sufeq(m_volume, volume, 1e-1f)) {
    m_volume = volume;

    if (m_audioInspectorOpened)
      this->setParams();
  }
}

void
AudioProcessor::setAudioCorrection(Suscan::Orbit const &orbit)
{
  m_orbit = orbit;

  if (m_correctionEnabled && m_audioInspectorOpened)
    m_analyzer->setInspectorDopplerCorrection(m_audioInspHandle, m_orbit);
}

void
AudioProcessor::setCorrectionEnabled(bool enabled)
{
  if (m_correctionEnabled != enabled) {
    m_correctionEnabled = enabled;

    if (m_correctionEnabled && m_audioInspectorOpened)
      m_analyzer->setInspectorDopplerCorrection(m_audioInspHandle, m_orbit);
  }
}

void
AudioProcessor::setDemod(AudioDemod demod)
{
  if (m_demod != demod) {
    m_demod = demod;

    if (m_audioInspectorOpened)
      this->setParams();
  }
}

void
AudioProcessor::setSampleRate(unsigned rate)
{
  // Seting the rate is a somewhat delicate process that involves
  // cancelling current audio samples and setting the config back

  if (m_sampleRate != rate) {
    m_sampleRate = rate;

    m_playBack->setSampleRate(rate);

    if (m_audioInspectorOpened) {
      m_settingRate = true;
      this->setParams();
    }
  }
}

void
AudioProcessor::setCutOff(float cutOff)
{
  if (!sufeq(m_cutOff, cutOff, 1e-8f)) {
    m_cutOff = cutOff;

    if (m_audioInspectorOpened)
      this->setParams();
  }
}

void
AudioProcessor::setDemodFreq(SUFREQ lo)
{
  // If changed, set frequency
  if (!sufeq(m_lo, lo, 1e-8f)) {
    m_lo = lo;

    if (m_audioInspectorOpened)
      m_analyzer->setInspectorFreq(m_audioInspHandle, m_lo);
  }
}

void
AudioProcessor::startRecording(QString)
{
  // TODO
}

void
AudioProcessor::stopRecording(void)
{
  // TODO
}

void
AudioProcessor::onInspectorMessage(Suscan::InspectorMessage const &msg)
{
  if (m_audioInspectorOpened && msg.getInspectorId() == m_audioInspId) {
    // This refers to us!

    switch (msg.getKind()) {
      case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG:
        // Async step 4: analyzer acknowledged config, emit audio open
        if (!m_opened) {
          m_opened = true;
          emit audioOpened();
        }

        // Check if this is the acknowledgement of a "Setting rate" message
        if (m_settingRate) {
          const suscan_config_t *cfg = msg.getCConfig();
          const struct suscan_field_value *value;

          value = suscan_config_get_value(cfg, "audio.sample-rate");

          // Value is the same as requested? Go ahead
          if (value != nullptr) {
            if (m_sampleRate == value->as_int)
              m_settingRate = false;
          } else {
            // This should never happen, but just in case the server is not
            // behaving as expected
            m_settingRate = false;
          }
        }
        break;


      case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND:
      case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT:
      case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE:
        if (!m_opened) {
          this->closeAudio();
          emit audioError("Unexpected error while opening audio channel");
        }

        break;

      default:
        break;
    }
  }
}

void
AudioProcessor::onInspectorSamples(Suscan::SamplesMessage const &msg)
{
  // Feed samples, only if the sample rate is right

  if (m_opened && msg.getInspectorId() == m_audioInspId) {
    const SUCOMPLEX *samples = msg.getSamples();
    unsigned int count = msg.getCount();
    std::vector<SUCOMPLEX> silence;

    // Sample rate is still changing, replace this message with silence to
    // prevent playing back stuff at the wrong rate
    if (m_settingRate) {
      silence.resize(count);
      std::fill(silence.begin(), silence.end(), 0);
      samples = silence.data();
    }

    m_playBack->write(samples, count);
  }
}

void
AudioProcessor::onOpened(
    Suscan::AnalyzerRequest const &req,
    const suscan_config_t *config)
{
  // Async step 2: set inspector parameters

  m_opening = false;

  if (m_analyzer != nullptr) {
    // We do a lazy initialization of the audio channel parameters. Instead of
    // creating our own audio configuration template in the constructor, we
    // wait for the channel to provide the current configuration and
    // duplicate that one.

    if (m_audioCfgTemplate != nullptr) {
      suscan_config_destroy(m_audioCfgTemplate);
      m_audioCfgTemplate = suscan_config_dup(config);

      if (m_audioCfgTemplate == nullptr) {
        m_analyzer->closeInspector(req.handle);
        emit audioError("Failed to duplicate audio configuration");
        return;
      }
    }

    // Async step 3: set parameters
    m_audioInspHandle      = req.handle;
    m_audioInspId          = req.inspectorId;
    m_audioInspectorOpened = true;

    this->setParams();

    if (m_correctionEnabled)
      m_analyzer->setInspectorDopplerCorrection(m_audioInspHandle, m_orbit);
  }
}

void
AudioProcessor::onCancelled(Suscan::AnalyzerRequest const &)
{
  m_opening = false;
  m_playBack->stop();
}

void
AudioProcessor::onError(Suscan::AnalyzerRequest const &, std::string const &err)
{
  m_opening = false;
  m_playBack->stop();

  emit audioError(
        "Failed to open audio channel: " + QString::fromStdString(err));
}
