//
//    SigDiggerHelpers.cpp: Various helping functions
//    Copyright (C) 2020 Gonzalo José Carracedo Carballal
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

#include "SigDiggerHelpers.h"
#include "DefaultGradient.h"
#include "Version.h"
#include <QComboBox>
#include <fstream>
#include <iomanip>
#include <QMessageBox>
#include <QFileDialog>
#include <SuWidgets/SuWidgetsHelpers.h>
#include <sigutils/matfile.h>

#ifndef SIGDIGGER_PKGVERSION
#  define SIGDIGGER_PKGVERSION \
  "custom build on " __DATE__ " at " __TIME__ " (" __VERSION__ ")"
#endif /* SUSCAN_BUILD_STRING */

using namespace SigDigger;

SigDiggerHelpers *SigDiggerHelpers::currInstance = nullptr;

SigDiggerHelpers *
SigDiggerHelpers::instance(void)
{
  if (currInstance == nullptr)
    currInstance = new SigDiggerHelpers();

  return currInstance;
}

QString
SigDiggerHelpers::version(void)
{
  return QString(SIGDIGGER_VERSION_STRING);
}

QString
SigDiggerHelpers::pkgversion(void)
{
  return QString(SIGDIGGER_PKGVERSION);
}


bool
SigDiggerHelpers::exportToMatlab(
    QString const &path,
    const SUCOMPLEX *data,
    int length,
    qreal fs,
    int start,
    int end)
{
  std::ofstream of(path.toStdString().c_str(), std::ofstream::binary);

  if (!of.is_open())
    return false;

  of << "%\n";
  of << "% Time domain capture file generated by SigDigger\n";
  of << "%\n\n";

  of << "sampleRate = " << fs << ";\n";
  of << "deltaT = " << 1 / fs << ";\n";
  of << "X = [ ";

  of << std::setprecision(std::numeric_limits<float>::digits10);

  if (start < 0)
    start = 0;
  if (end >= length)
    end = length - 1;

  for (int i = start; i <= end; ++i)
    of << SU_C_REAL(data[i]) << " + " << SU_C_IMAG(data[i]) << "i, ";

  of << "];\n";

  return true;
}

bool
SigDiggerHelpers::exportToMat5(
    QString const &path,
    const SUCOMPLEX *data,
    int length,
    qreal fs,
    int start,
    int end)
{
  su_mat_file_t *mf = nullptr;
  su_mat_matrix_t *mtx = nullptr;
  bool ok = false;

  if (start < 0)
    start = 0;
  if (end >= length)
    end = length - 1;

  SU_TRYCATCH(mf = su_mat_file_new(), goto done);

  SU_TRYCATCH(mtx = su_mat_file_make_matrix(mf, "sampleRate", 1, 1), goto done);
  SU_TRYCATCH(su_mat_matrix_write_col(mtx, fs), goto done);

  SU_TRYCATCH(mtx = su_mat_file_make_matrix(mf, "deltaT", 1, 1), goto done);
  SU_TRYCATCH(su_mat_matrix_write_col(mtx, 1 / fs), goto done);

  SU_TRYCATCH(
        mtx = su_mat_file_make_matrix(mf, "X", 2, end - start + 1),
        goto done);

  for (int i = start; i <= end; ++i)
    SU_TRYCATCH(
          su_mat_matrix_write_col_array(
            mtx,
            reinterpret_cast<const SUFLOAT *>(data + i)),
          goto done);

  SU_TRYCATCH(su_mat_file_dump(mf, path.toStdString().c_str()), goto done);

  ok = true;

done:
  if (mf != nullptr)
    su_mat_file_destroy(mf);

  return ok;
}

bool
SigDiggerHelpers::exportToWav(
    QString const &path,
    const SUCOMPLEX *data,
    int length,
    qreal fs,
    int start,
    int end)
{
  SF_INFO sfinfo;
  SNDFILE *sfp = nullptr;
  bool ok = false;

  sfinfo.channels = 2;
  sfinfo.samplerate = static_cast<int>(fs);
  sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

  if ((sfp = sf_open(path.toStdString().c_str(), SFM_WRITE, &sfinfo)) == nullptr)
    goto done;

  if (start < 0)
    start = 0;
  if (end > length)
    end = length;

  length = end - start;

  ok = sf_write_float(
        sfp,
        reinterpret_cast<const SUFLOAT *>(data + start),
        length << 1) == (length << 1);

done:
  if (sfp != nullptr)
    sf_close(sfp);

  return ok;
}

void
SigDiggerHelpers::openSaveSamplesDialog(
    QWidget *root,
    const SUCOMPLEX *data,
    size_t len,
    qreal fs,
    int start,
    int end)
{
  bool done = false;

  do {
    QFileDialog dialog(root);
    QStringList filters;

    dialog.setFileMode(QFileDialog::FileMode::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setWindowTitle(QString("Save capture"));

    filters << "MATLAB/Octave script (*.m)"
            << "MATLAB 5.0 MAT-file (*.mat)"
            << "Audio file (*.wav)";

    dialog.setNameFilters(filters);

    if (dialog.exec()) {
      QString path = dialog.selectedFiles().first();
      QString filter = dialog.selectedNameFilter();
      bool result;

      if (strstr(filter.toStdString().c_str(), ".mat") != nullptr)  {
        path = SuWidgetsHelpers::ensureExtension(path, "mat");
        result = SigDiggerHelpers::exportToMat5(
              path,
              data,
              static_cast<int>(len),
              fs,
              start,
              end);
      } else if (strstr(filter.toStdString().c_str(), ".m") != nullptr)  {
        path = SuWidgetsHelpers::ensureExtension(path, "m");
        result = SigDiggerHelpers::exportToMatlab(
              path,
              data,
              static_cast<int>(len),
              fs,
              start,
              end);
      } else {
        path = SuWidgetsHelpers::ensureExtension(path, "wav");
        result = SigDiggerHelpers::exportToWav(
              path,
              data,
              static_cast<int>(len),
              fs,
              start,
              end);
      }

      if (!result) {
        QMessageBox::warning(
              root,
              "Cannot open file",
              "Cannote save file in the specified location. Please choose "
              "a different location and try again.",
              QMessageBox::Ok);
      } else {
        done = true;
      }
    } else {
      done = true;
    }
  } while (!done);
}


Palette *
SigDiggerHelpers::getGqrxPalette(void)
{
  static qreal color[256][3];
  if (this->gqrxPalette == nullptr) {
    for (int i = 0; i < 256; i++) {
      if (i < 20) { // level 0: black background
        color[i][0] = color[i][1] = color[i][2] = 0;
      } else if ((i >= 20) && (i < 70)) { // level 1: black -> blue
        color[i][0] = color[i][1] = 0;
        color[i][2] = (140*(i-20)/50) / 255.;
      } else if ((i >= 70) && (i < 100)) { // level 2: blue -> light-blue / greenish
        color[i][0] = (60*(i-70)/30) / 255.;
        color[i][1] = (125*(i-70)/30) / 255.;
        color[i][2] = (115*(i-70)/30 + 140) / 255.;
      } else if ((i >= 100) && (i < 150)) { // level 3: light blue -> yellow
        color[i][0] = (195*(i-100)/50 + 60) / 255.;
        color[i][1] = (130*(i-100)/50 + 125) / 255.;
        color[i][2] = (255-(255*(i-100)/50)) / 255.;
      } else if ((i >= 150) && (i < 250)) { // level 4: yellow -> red
        color[i][0] = 1;
        color[i][1] = (255-255*(i-150)/100) / 255.;
        color[i][2] = 0;
      } else if (i >= 250) { // level 5: red -> white
        color[i][0] = 1;
        color[i][1] = (255*(i-250)/5) / 255.;
        color[i][2] = (255*(i-250)/5) / 255.;
      }
    }

    gqrxPalette = new Palette("Gqrx", color);
  }

  return gqrxPalette;
}

void
SigDiggerHelpers::deserializePalettes(void)
{
  Suscan::Singleton *sus = Suscan::Singleton::get_instance();

  if (this->palettes.size() == 0) {
    this->palettes.push_back(Palette("Suscan", wf_gradient));
    this->palettes.push_back(*this->getGqrxPalette());
  }

  // Fill palette vector
  for (auto i = sus->getFirstPalette();
       i != sus->getLastPalette();
       i++)
    this->palettes.push_back(Palette(*i));
}

void
SigDiggerHelpers::populatePaletteCombo(QComboBox *cb)
{
  int ndx = 0;

  cb->clear();

  // Populate combo
  for (auto p : this->palettes) {
    cb->insertItem(
          ndx,
          QIcon(QPixmap::fromImage(p.getThumbnail())),
          QString::fromStdString(p.getName()),
          QVariant::fromValue(ndx));
    ++ndx;
  }
}

const Palette *
SigDiggerHelpers::getPalette(int index) const
{
  if (index < 0 || index >= static_cast<int>(this->palettes.size()))
    return nullptr;

  return &this->palettes[static_cast<size_t>(index)];
}

int
SigDiggerHelpers::getPaletteIndex(std::string const &name) const
{
  unsigned int i;

  for (i = 0; i < this->palettes.size(); ++i)
    if (this->palettes[i].getName().compare(name) == 0)
      return static_cast<int>(i);

  return -1;
}

const Palette *
SigDiggerHelpers::getPalette(std::string const &name) const
{
  int index = this->getPaletteIndex(name);

  if (index >= 0)
    return &this->palettes[index];

  return nullptr;
}

SigDiggerHelpers::SigDiggerHelpers()
{
  this->deserializePalettes();
}

void
SigDiggerHelpers::kahanMeanAndRms(
    SUCOMPLEX *mean,
    SUFLOAT *rms,
    const SUCOMPLEX *data,
    int length)
{
  SUCOMPLEX meanSum = 0;
  SUCOMPLEX meanC   = 0;
  SUCOMPLEX meanY, meanT;

  SUFLOAT   rmsSum = 0;
  SUFLOAT   rmsC   = 0;
  SUFLOAT   rmsY, rmsT;

  for (int i = 0; i < length; ++i) {
    meanY = data[i] - meanC;
    rmsY  = SU_C_REAL(data[i] * SU_C_CONJ(data[i])) - rmsC;

    meanT = meanSum + meanY;
    rmsT  = rmsSum  + rmsY;

    meanC = (meanT - meanSum) - meanY;
    rmsC  = (rmsT  - rmsSum)  - rmsY;

    meanSum = meanT;
    rmsSum  = rmsT;
  }

  *mean = meanSum / SU_ASFLOAT(length);
  *rms  = SU_SQRT(rmsSum / length);
}

void
SigDiggerHelpers::calcLimits(
    SUCOMPLEX *oMin,
    SUCOMPLEX *oMax,
    const SUCOMPLEX *data,
    int length)
{
  SUFLOAT minReal = +std::numeric_limits<SUFLOAT>::infinity();
  SUFLOAT maxReal = -std::numeric_limits<SUFLOAT>::infinity();
  SUFLOAT minImag = minReal;
  SUFLOAT maxImag = maxReal;

  for (int i = 0; i < length; ++i) {
    if (SU_C_REAL(data[i]) < minReal)
      minReal = SU_C_REAL(data[i]);
    if (SU_C_IMAG(data[i]) < minImag)
      minImag = SU_C_IMAG(data[i]);

    if (SU_C_REAL(data[i]) > maxReal)
      maxReal = SU_C_REAL(data[i]);
    if (SU_C_IMAG(data[i]) > maxImag)
      maxImag = SU_C_IMAG(data[i]);
  }

  *oMin = minReal + I * minImag;
  *oMax = maxReal + I * maxImag;
}

