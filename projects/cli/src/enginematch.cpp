/*
    This file is part of Cute Chess.

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Cute Chess is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Cute Chess.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "econode.h"
#include "board/board.h"

#include "enginematch.h"
#include <QtMath>
#include <QList>
#include <QMultiMap>
#include <QTextCodec>
#include <chessplayer.h>
#include <playerbuilder.h>
#include <chessgame.h>
#include <polyglotbook.h>
#include <tournament.h>
#include <gamemanager.h>
#include <sprt.h>
#include <jsonparser.h>
#include <jsonserializer.h>

EngineMatch::EngineMatch(Tournament* tournament, QObject* parent)
	: QObject(parent),
	  m_tournament(tournament),
	  m_debug(false),
	  m_ratingInterval(0),
	  m_bookMode(OpeningBook::Ram),
	  m_eloKfactor(32.0),
	  m_pgnFormat(true),
	  m_jsonFormat(true)
{
	Q_ASSERT(tournament != nullptr);

	m_startTime.start();
}

EngineMatch::~EngineMatch()
{
	qDeleteAll(m_books);
}

OpeningBook* EngineMatch::addOpeningBook(const QString& fileName)
{
	if (fileName.isEmpty())
		return nullptr;

	if (m_books.contains(fileName))
		return m_books[fileName];

	PolyglotBook* book = new PolyglotBook(m_bookMode);
	if (!book->read(fileName))
	{
		delete book;
		qWarning("Can't read opening book file %s", qPrintable(fileName));
		return nullptr;
	}

	m_books[fileName] = book;
	return book;
}

void EngineMatch::start()
{
	connect(m_tournament, SIGNAL(finished()),
		this, SLOT(onTournamentFinished()));
	connect(m_tournament, SIGNAL(gameStarted(ChessGame*, int, int, int)),
		this, SLOT(onGameStarted(ChessGame*, int)));
	connect(m_tournament, SIGNAL(gameFinished(ChessGame*, int, int, int)),
		this, SLOT(onGameFinished(ChessGame*, int)));

	if (m_debug)
		connect(m_tournament->gameManager(), SIGNAL(debugMessage(QString)),
			this, SLOT(print(QString)));

	QMetaObject::invokeMethod(m_tournament, "start", Qt::QueuedConnection);
}

void EngineMatch::stop()
{
	QMetaObject::invokeMethod(m_tournament, "stop", Qt::QueuedConnection);
}

void EngineMatch::setDebugMode(bool debug)
{
	m_debug = debug;
}

void EngineMatch::setRatingInterval(int interval)
{
	Q_ASSERT(interval >= 0);
	m_ratingInterval = interval;
}

void EngineMatch::setBookMode(OpeningBook::AccessMode mode)
{
	m_bookMode = mode;
}

void EngineMatch::setTournamentFile(QString& tournamentFile)
{
	m_tournamentFile = tournamentFile;
}

void EngineMatch::setEloKfactor(qreal eloKfactor)
{
	m_eloKfactor = eloKfactor;
}

void EngineMatch::setOutputFormats(bool pgnFormat, bool jsonFormat)
{
	m_pgnFormat = pgnFormat;
	m_jsonFormat = jsonFormat;
}

void EngineMatch::generateSchedule(QVariantList& pList)
{
	QList< QPair<QString, QString> > pairings = m_tournament->getPairings();
	if (pairings.isEmpty()) return;

	QString scheduleFile(m_tournamentFile);
	scheduleFile = scheduleFile.remove(".json") + "_schedule";

	if (m_jsonFormat) {
		const QString tempName(scheduleFile + "_temp.json");
		const QString finalName(scheduleFile + ".json");
		if (QFile::exists(tempName))
			QFile::remove(tempName);
		QFile output(tempName);
		if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
			qWarning("cannot open schedule JSON file: %s", qPrintable(tempName));
			return;
		}
		QTextStream out(&output);
		QVariantMap pMap;
		QVariantList sList;
		QList< QPair<QString, QString> >::iterator i;
		int count = 0;
		for (i = pairings.begin(); i != pairings.end(); ++i, ++count) {
			QVariantMap	sMap;
			QString opening;

			if (count < pList.size()) {
				pMap = pList.at(count).toMap();
				if (pMap.contains("white"))
					sMap["White"] = pMap["white"];
				if (pMap.contains("black"))
					sMap["Black"] = pMap["black"];
				if (pMap.contains("startTime"))
					sMap["Start"] = pMap["startTime"];
				if (pMap.contains("result"))
					sMap["Result"] = pMap["result"];
				if (pMap.contains("terminationDetails"))
					sMap["Termination"] = pMap["terminationDetails"];
				if (pMap.contains("gameDuration"))
					sMap["Duration"] = pMap["gameDuration"];
				if (pMap.contains("finalFen"))
					sMap["FinalFen"] = pMap["finalFen"];
				if (pMap.contains("ECO"))
					sMap["ECO"] = pMap["ECO"];
				if (pMap.contains("opening"))
					opening = pMap["opening"].toString();
				if (pMap.contains("variation")) {
					QString variation = pMap["variation"].toString();
					if (!variation.isEmpty())
						opening += ", " + variation;
				}
				if (!opening.isEmpty())
					sMap["Opening"] = opening;
				if (pMap.contains("plyCount"))
					sMap["Moves"] = pMap["plyCount"];
				if (pMap.contains("whiteEval"))
					sMap["WhiteEv"] = pMap["whiteEval"];
				if (pMap.contains("blackEval")) {
					QString blackEval = pMap["blackEval"].toString();
					if (blackEval.at(0) == '-')
						blackEval.remove(0, 1);
					else if (blackEval != "0.00")
						blackEval = "-" + blackEval;
					sMap["BlackEv"] = blackEval;
				}
			} else {
				sMap["White"] = i->first;
				sMap["Black"] = i->second;
			}
			sMap["Game"] = count + 1;
			sList.append(sMap);
		}

		JsonSerializer serializer(sList);
		serializer.serialize(out);
		output.close();
		if (QFile::exists(finalName))
			QFile::remove(finalName);
		if (!QFile::rename(tempName, finalName))
			qWarning("cannot rename schedule JSON file: %s to %s", qPrintable(tempName), qPrintable(finalName));
	}

	if (m_pgnFormat) {
		const QString tempName(scheduleFile + "_temp.txt");
		const QString finalName(scheduleFile + ".txt");
		if (QFile::exists(tempName))
			QFile::remove(tempName);
		QFile output(tempName);
		if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
			qWarning("cannot open schedule TXT file: %s", qPrintable(tempName));
			return;
		}
		QTextStream out(&output);
		QVariantMap pMap;
		int maxName = 5, maxTerm = 11, maxFen = 9;
		for (int i = 0; i < pList.size(); i++) {
			int len;
			pMap = pList.at(i).toMap();
			if (pMap.contains("terminationDetails")) {
				len = pMap["terminationDetails"].toString().length();
				if (len > maxTerm) maxTerm = len;
			}
			if (pMap.contains("finalFen")) {
				len = pMap["finalFen"].toString().length();
				if (len > maxFen) maxFen = len;
			}
		}

		// now check the player list for maxName
		int playerCount = m_tournament->playerCount();
		for (int i = 0; i < playerCount; i++) {
			int len = m_tournament->playerAt(i).builder()->name().length();
			if (len > maxName) maxName = len;
		}

		QString scheduleText;
		scheduleText = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14\n")
			.arg("Nr", pairings.size() >= 100 ? 3 : 2)
			.arg("White", maxName)
			.arg("", 3)
			.arg("", -3)
			.arg("Black", -maxName)
			.arg("Termination", -maxTerm)
			.arg("Mov", 3)
			.arg("WhiteEv", 7)
			.arg("BlackEv", -7)
			.arg("Start", -22)
			.arg("Duration", 8)
			.arg("ECO", 3)
			.arg("FinalFen", -maxFen)
			.arg("Opening");

		QList< QPair<QString, QString> >::iterator i;
		int count = 0;
		for (i = pairings.begin(); i != pairings.end(); ++i, ++count) {
			QString whiteName, blackName, whiteResult, blackResult, termination, startTime, duration, ECO, finalFen, opening;
			QString whiteEval, blackEval;
			QString plies = 0;

			whiteName = i->first;
			blackName = i->second;

			if (count < pList.size()) {
				pMap = pList.at(count).toMap();
				if (!pMap.isEmpty()) {
					if (pMap.contains("white")) // TODO error check against above
						whiteName = pMap["white"].toString();
					if (pMap.contains("black"))
						blackName = pMap["black"].toString();
					if (pMap.contains("startTime"))
						startTime = pMap["startTime"].toString();
					if (pMap.contains("result")) {
						QString result = pMap["result"].toString();
						if (result == "*") {
							whiteResult = blackResult = result;
						} else if (result == "1-0") {
							whiteResult = "1";
							blackResult = "0";
						} else if (result == "0-1") {
							blackResult = "1";
							whiteResult = "0";
						} else {
							whiteResult = blackResult = "1/2";
						}
					}
					if (pMap.contains("terminationDetails"))
						termination = pMap["terminationDetails"].toString();
					if (pMap.contains("gameDuration"))
						duration = pMap["gameDuration"].toString();
					if (pMap.contains("finalFen"))
						finalFen = pMap["finalFen"].toString();
					if (pMap.contains("ECO"))
						ECO = pMap["ECO"].toString();
					if (pMap.contains("opening"))
						opening = pMap["opening"].toString();
					if (pMap.contains("variation")) {
						QString variation = pMap["variation"].toString();
						if (!variation.isEmpty())
							opening += ", " + variation;
					}
					if (pMap.contains("plyCount"))
						plies = pMap["plyCount"].toString();
					if (pMap.contains("whiteEval"))
						whiteEval = pMap["whiteEval"].toString();
					if (pMap.contains("blackEval")) {
						blackEval = pMap["blackEval"].toString();
						if (blackEval.at(0) == '-') {
							blackEval.remove(0, 1);
						} else {
							if (blackEval != "0.00")
								blackEval = "-" + blackEval;
						}
					}
				}
			}
			scheduleText += QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14\n")
				.arg(QString::number(count+1), pairings.size() >= 100 ? 3 : 2)
				.arg(whiteName, maxName)
				.arg(whiteResult, 3)
				.arg(blackResult, -3)
				.arg(blackName, -maxName)
				.arg(termination, -maxTerm)
				.arg(plies, 3)
				.arg(whiteEval, 7)
				.arg(blackEval, -7)
				.arg(startTime, -22)
				.arg(duration, 8)
				.arg(ECO, 3)
				.arg(finalFen, -maxFen)
				.arg(opening);
		}
		out.setCodec("ISO 8859-1"); // output is converted to ASCII
		out << scheduleText;
		output.close();
		if (QFile::exists(finalName))
			QFile::remove(finalName);
		if (!QFile::rename(tempName, finalName))
			qWarning("cannot rename schedule TXT file: %s to %s", qPrintable(tempName), qPrintable(finalName));
	}
}

struct CrossTableData
{
public:

	CrossTableData(QString engineName, int elo = 0) :
		m_score(0),
		m_neustadtlScore(0),
		m_rating(elo),
		m_gamesPlayedAsWhite(0),
		m_gamesPlayedAsBlack(0),
		m_winsAsWhite(0),
		m_winsAsBlack(0),
		m_performance(0),
		m_elo(0)
	{
		m_engineName = engineName;
	};

	CrossTableData() :
		m_score(0),
		m_neustadtlScore(0),
		m_rating(0),
		m_gamesPlayedAsWhite(0),
		m_gamesPlayedAsBlack(0),
		m_winsAsWhite(0),
		m_winsAsBlack(0),
		m_performance(0),
		m_elo(0)
	{

	};

	bool isEmpty() { return m_engineName.isEmpty(); }

	QString m_engineName;
	QString m_engineAbbrev;
	double m_score;
	double m_neustadtlScore;
	int m_rating;
	int m_gamesPlayedAsWhite;
	int m_gamesPlayedAsBlack;
	int m_winsAsWhite;
	int m_winsAsBlack;
	double m_performance;
	double m_elo;
	QMap<QString, QString> m_tableData;
};

bool sortCrossTableDataByScore(const CrossTableData &s1, const CrossTableData &s2)
{
	if (s1.m_score == s2.m_score) {
		if (s1.m_neustadtlScore == s2.m_neustadtlScore) {
			if (s1.m_gamesPlayedAsBlack == s2.m_gamesPlayedAsBlack) {
				if ((s1.m_winsAsWhite + s1.m_winsAsBlack) == (s2.m_winsAsWhite + s2.m_winsAsBlack)) {
					return (s1.m_winsAsBlack > s2.m_winsAsBlack);
				} else {
					return (s1.m_winsAsWhite + s1.m_winsAsBlack) > (s2.m_winsAsWhite + s2.m_winsAsBlack);
				}
			} else {
				return s1.m_gamesPlayedAsBlack > s2.m_gamesPlayedAsBlack;
			}
		} else {
			return s1.m_neustadtlScore > s2.m_neustadtlScore;
		}
	}
	return s1.m_score > s2.m_score;
}

void EngineMatch::generateCrossTable(QVariantList& pList)
{
	const int playerCount = m_tournament->playerCount();
	QMap<QString, CrossTableData> ctMap;
	QStringList abbrevList;
	int roundLength = 2;
	int maxName = 6;

	// ensure names and abbreviations
	for (int i = 0; i < playerCount; i++) {
		CrossTableData ctd(m_tournament->playerAt(i).builder()->name(), m_tournament->playerAt(i).builder()->rating());
		if (ctd.m_engineName.length() > maxName) maxName = ctd.m_engineName.length();

		int n = 1;
		QString abbrev;
		abbrev.append(ctd.m_engineName.at(0).toUpper()).append(ctd.m_engineName.length() > n ? ctd.m_engineName.at(n++).toLower() : ' ');
		while (abbrevList.contains(abbrev)) {
			abbrev[1] = ctd.m_engineName.length() > n ? ctd.m_engineName.at(n++).toLower() : ' ';
		}
		ctd.m_engineAbbrev = abbrev;
		abbrevList.append(abbrev);
		ctMap.insert(ctd.m_engineName, ctd);
	}

	// calculate scores and crosstable strings
	for (int i = 0; i < pList.size(); i++) {
		QVariantMap pMap = pList.at(i).toMap();
		if (pMap.contains("white") && pMap.contains("black") && pMap.contains("result")) {
			QString whiteName = pMap["white"].toString();
			QString blackName = pMap["black"].toString();
			QString result = pMap["result"].toString();
			CrossTableData& whiteData = ctMap[whiteName];
			CrossTableData& blackData = ctMap[blackName];
			QString& whiteDataString = whiteData.m_tableData[blackName];
			QString& blackDataString = blackData.m_tableData[whiteName];

			if (result == "*") {
				continue; // game in progress or invalid or something
			}
			if (result == "1-0") {
				whiteData.m_score += 1;
				whiteData.m_winsAsWhite++;
				whiteDataString += "1";
				blackDataString += "0";
			} else if (result == "0-1") {
				blackData.m_score += 1;
				blackData.m_winsAsBlack++;
				whiteDataString += "0";
				blackDataString += "1";
			} else if (result == "1/2-1/2") {
				whiteData.m_score += 0.5;
				blackData.m_score += 0.5;
				whiteDataString += "=";
				blackDataString += "=";
			}
			if (whiteDataString.length() > roundLength) roundLength = whiteDataString.length();
			if (blackDataString.length() > roundLength) roundLength = blackDataString.length();
			whiteData.m_gamesPlayedAsWhite++;
			blackData.m_gamesPlayedAsBlack++;
		}
	}
	// calculate SB
	QMapIterator<QString, CrossTableData> ct(ctMap);
	qreal largestSB = 1.0;
	qreal largestScore = 1.0;
	while (ct.hasNext()) {
		ct.next();
		CrossTableData& ctd = ctMap[ct.key()];
		QMapIterator<QString, QString> td(ctd.m_tableData);
		qreal sb = 0.0;
		while (td.hasNext()) {
			td.next();
			QString::ConstIterator c = td.value().begin();
			while (c != td.value().end()) {
				if (*c == QChar('1')) {
					sb += ctMap[td.key()].m_score;
				} else if (*c == QChar('=')) {
					sb += ctMap[td.key()].m_score / 2.;
				}
				c++;
			}
		}
		ctd.m_neustadtlScore = sb;
		if (ctd.m_neustadtlScore > largestSB) largestSB = ctd.m_neustadtlScore;
		if (ctd.m_score > largestScore) largestScore = ctd.m_score;
	}
	// calculate Elo and point rate
	qreal maxElo = 1;
	qreal largestPerf = 0.0001;
	int maxGames = 1;
	ct.toFront();
	while (ct.hasNext()) {
		ct.next();
		CrossTableData& ctd = ctMap[ct.key()];

		QMapIterator<QString, CrossTableData> ot(ct);
		while (ot.hasNext()) {
			ot.next();
			CrossTableData& otd = ctMap[ot.key()];
			const QString& tds = ctd.m_tableData[ot.key()];

			int score = 0;
			int games = 0;
			for (QString::ConstIterator c = tds.begin(); c != tds.end(); ++c)
				switch(c->toLatin1()) {
				case '1':
					score += 2;
					++games;
					break;
				case '=':
					++score;
					++games;
					break;
				case '0':
					++games;
					break;
				default:
					break;
				}

			if (games > 0) {
				const qreal real = static_cast<qreal>(score) / (games * 2);
				const qreal expected = 1.0 / (1.0 + qPow(10.0, (otd.m_rating - ctd.m_rating) / 400.0));
				const qreal elo =  m_eloKfactor * (real - expected) * games;

				ctd.m_elo += elo;
				otd.m_elo -= elo;
			}
		}

		const int totGames = ctd.m_gamesPlayedAsWhite + ctd.m_gamesPlayedAsBlack;
		if (totGames > 0) {
			ctd.m_performance = ctd.m_score / totGames;

			if (ctd.m_performance > largestPerf)
				largestPerf = ctd.m_performance;

			const qreal totElo = ctd.m_elo < 0 ? -ctd.m_elo : ctd.m_elo;
			if (totElo > maxElo)
				maxElo = totElo;

			if (totGames > maxGames)
				maxGames = totGames;
		}
	}

	QString crossTableFile(m_tournamentFile);
	crossTableFile = crossTableFile.remove(".json") + "_crosstable";

	QList<CrossTableData> list = ctMap.values();
	qSort(list.begin(), list.end(), sortCrossTableDataByScore);
	QList<CrossTableData>::iterator i;

	if (m_jsonFormat) {
		const QString tempName(crossTableFile + "_temp.json");
		const QString finalName(crossTableFile + ".json");
		if (QFile::exists(tempName))
			QFile::remove(tempName);
		QFile output(tempName);
		if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
			qWarning("cannot open crosstable JSON file: %s", qPrintable(tempName));
			return;
		}
		QTextStream out(&output);

		QVariantMap cMap;
		QVariantList order;
		for (i = list.begin(); i != list.end(); ++i)
			order << i->m_engineName;
		cMap["Order"] = order;

		QVariantMap	table;
		int rank = 1;
		for (i = list.begin(); i != list.end(); ++i, ++rank) {
			QVariantMap obj;
			QVariantMap results;
			obj["Rank"] = rank;
			obj["Abbreviation"] = i->m_engineAbbrev;
			obj["Rating"] = i->m_rating;
			obj["Score"] = i->m_score;
			obj["GamesAsWhite"] = i->m_gamesPlayedAsWhite;
			obj["GamesAsBlack"] = i->m_gamesPlayedAsBlack;
			obj["Games"] = i->m_gamesPlayedAsWhite + i->m_gamesPlayedAsBlack;
			obj["Neustadtl"] = i->m_neustadtlScore;
			obj["Performance"] = i->m_performance * 100.0;
			obj["Elo"] = i->m_elo;
			for(const QVariant& eVar : order) {
				const QString engineName(eVar.toString());
				if (engineName == i->m_engineName)
					continue;
				QVariantMap result;
				QVariantList scores;
				for (const QChar& ch : i->m_tableData[engineName])
					switch (ch.toLatin1()) {
					case '1':
						scores << 1.0;
						break;
					case '=':
						scores << 0.5;
						break;
					case '0':
						scores << 0.0;
						break;
					default:
						break;
					}
				result["Text"] = i->m_tableData[engineName];
				result["Scores"] = scores;
				results[engineName] = result;
			}

			obj["Results"] = results;
			table[i->m_engineName] = obj;
		}
		cMap["Table"] = table;

		JsonSerializer serializer(cMap);
		serializer.serialize(out);
		output.close();
		if (QFile::exists(finalName))
			QFile::remove(finalName);
		if (!QFile::rename(tempName, finalName))
			qWarning("cannot rename crosstable JSON file: %s to %s", qPrintable(tempName), qPrintable(finalName));
	}

	if (m_pgnFormat) {
		if (playerCount == 2) {
			roundLength = 2;
			QVariantMap pMap = pList.at(0).toMap();
			if (pMap.contains("white") && pMap.contains("black")) {
				QString whiteName = pMap["white"].toString();
				QString blackName = pMap["black"].toString();
				CrossTableData& whiteData = ctMap[whiteName];
				CrossTableData& blackData = ctMap[blackName];
				QString& whiteDataString = whiteData.m_tableData[blackName];
				QString& blackDataString = blackData.m_tableData[whiteName];
				int whiteWin = 0;
				int whiteLose = 0;
				int whiteDraw = 0;

				for (int j = 0; j < whiteDataString.length(); j++) {
					if (whiteDataString[j] == '1')
						whiteWin++;
					else if (whiteDataString[j] == '0')
						whiteLose++;
					else
						whiteDraw++;
				}
				whiteDataString = QString("+ %1 = %2 - %3")
					.arg(whiteWin)
					.arg(whiteDraw)
					.arg(whiteLose);
				blackDataString = QString("+ %1 = %2 - %3")
					.arg(whiteLose)
					.arg(whiteDraw)
					.arg(whiteWin);

				if (whiteDataString.length() > roundLength) roundLength = whiteDataString.length();
				if (blackDataString.length() > roundLength) roundLength = blackDataString.length();
			}
		}

		int maxScore = qFloor(qLn(largestScore) * M_LOG10E) + 3;
		if (maxScore < 3)
			maxScore = 3;
		int maxSB = qFloor(qLn(largestSB) * M_LOG10E) + 4;
		if (maxSB < 4)
			maxSB = 4;
		maxGames = qFloor(qLn(maxGames) * M_LOG10E) + 1;
		if (maxGames < 2)
			maxGames = 2;
		int maxPerf = qFloor(qLn(largestPerf * 100.0) * M_LOG10E) + 3;
		if (maxPerf < 4)
			maxPerf = 4;
		maxElo = qFloor(qLn(maxElo) * M_LOG10E) + 2;
		if (maxElo < 3)
			maxElo = 3;
		QString crossTableHeaderText = QString("%1 %2 %3 %4 %5 %6 %7 %8")
			.arg("N", 2)
			.arg("Engine", -maxName)
			.arg("Rtng", 4)
			.arg("Pts", maxScore)
			.arg("Gm", maxGames)
			.arg("SB", maxSB)
			.arg("Elo", maxElo)
			.arg("Perf", maxPerf);

		QString eloText;
		QString crossTableBodyText;

		int count = 1;
		for (i = list.begin(); i != list.end(); ++i, ++count) {
			crossTableHeaderText += QString(" %1").arg(i->m_engineAbbrev, -roundLength);

			eloText = i->m_elo > 0 ? "+" : "";
			eloText += QString::number(i->m_elo, 'f', 0);
			crossTableBodyText += QString("%1 %2 %3 %4 %5 %6 %7 %8")
				.arg(count, 2)
				.arg(i->m_engineName, -maxName)
				.arg(i->m_rating, 4)
				.arg(i->m_score, maxScore, 'f', 1)
				.arg(i->m_gamesPlayedAsWhite + i->m_gamesPlayedAsBlack, maxGames)
				.arg(i->m_neustadtlScore, maxSB, 'f', 2)
				.arg(eloText, maxElo)
				.arg(i->m_performance * 100.0, maxPerf, 'f', 1);

			QList<CrossTableData>::iterator j;
			for (j = list.begin(); j != list.end(); ++j) {
				if (j->m_engineName == i->m_engineName) {
					crossTableBodyText += " ";
					int rl = roundLength;
					while(rl--) crossTableBodyText += "\u00B7";
				} else crossTableBodyText += QString(" %1").arg(i->m_tableData[j->m_engineName], -roundLength);
			}
			crossTableBodyText += "\n";
		}

		QString crossTableText = crossTableHeaderText + "\n\n" + crossTableBodyText;

		const QString tempName(crossTableFile + "_temp.txt");
		const QString finalName(crossTableFile + ".txt");
		if (QFile::exists(tempName))
			QFile::remove(tempName);
		QFile output(tempName);
		if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
			qWarning("cannot open tournament crosstable file: %s", qPrintable(tempName));
		} else {
			QTextStream out(&output);
			out.setCodec("UTF-8"); // otherwise output is converted to ASCII
			out << crossTableText;
			output.close();
			if (QFile::exists(finalName))
				QFile::remove(finalName);
			if (!QFile::rename(tempName, finalName))
				qWarning("cannot rename crosstable file: %s to %s", qPrintable(tempName), qPrintable(finalName));
		}
	}
}

void EngineMatch::onGameStarted(ChessGame* game, int number)
{
	Q_ASSERT(game != nullptr);

	qInfo("Started game %d of %d (%s vs %s)",
	      number,
	      m_tournament->finalGameCount(),
	      qUtf8Printable(game->player(Chess::Side::White)->name()),
	      qUtf8Printable(game->player(Chess::Side::Black)->name()));

	if (!m_tournamentFile.isEmpty()) {
		QVariantMap tfMap;
		if (QFile::exists(m_tournamentFile)) {
			QFile input(m_tournamentFile);
			if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
				qWarning("cannot open tournament configuration file: %s", qPrintable(m_tournamentFile));
				return;
			}

			QTextStream stream(&input);
			JsonParser jsonParser(stream);
			tfMap = jsonParser.parse().toMap();
		}

		QVariantList pList;
		if (!tfMap.isEmpty()) {
			if (tfMap.contains("matchProgress")) {
				pList = tfMap["matchProgress"].toList();
				int length = pList.length();
				if (length >= number) {
					qWarning("game %d already exists, deleting", number);
					while(length-- >= number) {
						pList.removeLast();
					}
				}
			}
		}

		QVariantMap pMap;
		pMap.insert("index", number);
		pMap.insert("white", game->player(Chess::Side::White)->name());
		pMap.insert("black", game->player(Chess::Side::Black)->name());
		QDateTime qdt = QDateTime::currentDateTime();
		pMap.insert("startTime", qdt.toString("HH:mm:ss' on 'yyyy.MM.dd"));
		pMap.insert("result", "*");
		pMap.insert("terminationDetails", "in progress");
		pList.append(pMap);
		tfMap.insert("matchProgress", pList);
		{
			QFile output(m_tournamentFile);
			if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
				qWarning("cannot open tournament configuration file: %s", qPrintable(m_tournamentFile));
			} else {
				QTextStream out(&output);
				JsonSerializer serializer(tfMap);
				serializer.serialize(out);
			}
		}
		generateSchedule(pList);
		generateCrossTable(pList);
	}
}

void EngineMatch::onGameFinished(ChessGame* game, int number)
{
	Q_ASSERT(game != nullptr);

	Chess::Result result(game->result());
	qInfo("Finished game %d (%s vs %s): %s",
	      number,
	      qUtf8Printable(game->player(Chess::Side::White)->name()),
	      qUtf8Printable(game->player(Chess::Side::Black)->name()),
	      qUtf8Printable(result.toVerboseString()));

	if (!m_tournamentFile.isEmpty()) {
		QVariantMap tfMap;

		if (QFile::exists(m_tournamentFile)) {
			QFile input(m_tournamentFile);
			if (!input.open(QIODevice::ReadOnly | QIODevice::Text)) {
				qWarning("cannot open tournament configuration file: %s", qPrintable(m_tournamentFile));
			} else {
				QTextStream stream(&input);
				JsonParser jsonParser(stream);
				tfMap = jsonParser.parse().toMap();
			}

			QVariantMap pMap;
			QVariantList pList;

			if (!tfMap.isEmpty()) {
				if (tfMap.contains("matchProgress")) {
					pList = tfMap["matchProgress"].toList();
					int length = pList.length();
					if (length < number) {
						qWarning("game %d doesn't exist", number);
					} else
						pMap = pList.at(number-1).toMap();
				}
			}

			if (!pMap.isEmpty()) {
				pMap.insert("result", result.toShortString());
				pMap.insert("terminationDetails", result.shortDescription());
				PgnGame *pgn = game->pgn();
				if (pgn) {
					// const EcoInfo eco = pgn->eco();
					QString val;
					val = pgn->tagValue("ECO");
					if (!val.isEmpty()) pMap.insert("ECO", val);
					val = pgn->tagValue("Opening");
					if (!val.isEmpty()) pMap.insert("opening", val);
					val = pgn->tagValue("Variation");
					if (!val.isEmpty()) pMap.insert("variation", val);
					// TODO: after TCEC is over, change this to moveCount, since that's what it is
					pMap.insert("plyCount", (game->moves().size() + 1) / 2);
					pMap.insert("gameDuration", pgn->gameDuration().toString("hh:mm:ss"));
				}
				pMap.insert("finalFen", game->board()->fenString());

				MoveEvaluation eval;
				QString sScore;
				const Chess::Side sides[] = { Chess::Side::White, Chess::Side::Black, Chess::Side::NoSide };

				for (int i = 0; sides[i] != Chess::Side::NoSide; i++) {
					Chess::Side side = sides[i];
					eval = game->player(side)->evaluation();
					int score = eval.score();
					int absScore = qAbs(score);

					// Detect out-of-range scores
					if (absScore > 99999)
						sScore = score < 0 ? "-999.99" : "999.99";
					else if (absScore > 9900	// Detect mate-in-n scores
						&& (absScore = 1000 - (absScore % 1000)) < 100)
					{
						sScore = score < 0 ? "-" : "";
						sScore += "M" + QString::number(absScore);
					}
					else
						sScore = QString::number(double(score) / 100.0, 'f', 2);

					if (side == Chess::Side::White)
						pMap.insert("whiteEval", sScore);
					else
						pMap.insert("blackEval", sScore);
				}

				pList.replace(number-1, pMap);
				tfMap.insert("matchProgress", pList);

				QFile output(m_tournamentFile);
				if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
					qWarning("cannot open tournament configuration file: %s", qPrintable(m_tournamentFile));
				} else {
					QTextStream out(&output);
					JsonSerializer serializer(tfMap);
					serializer.serialize(out);
				}
				generateSchedule(pList);
				generateCrossTable(pList);
			}
		}
	}

	if (m_tournament->playerCount() == 2)
	{
		TournamentPlayer fcp = m_tournament->playerAt(0);
		TournamentPlayer scp = m_tournament->playerAt(1);
		int totalResults = fcp.gamesFinished();
		qInfo("Score of %s vs %s: %d - %d - %d  [%.3f] %d",
		      qUtf8Printable(fcp.name()),
		      qUtf8Printable(scp.name()),
		      fcp.wins(), scp.wins(), fcp.draws(),
		      double(fcp.score()) / (totalResults * 2),
		      totalResults);
	}

	if (m_ratingInterval != 0
	&&  (m_tournament->finishedGameCount() % m_ratingInterval) == 0)
		printRanking();
}

void EngineMatch::onTournamentFinished()
{
	if (m_ratingInterval == 0
	||  m_tournament->finishedGameCount() % m_ratingInterval != 0)
		printRanking();

	QString error = m_tournament->errorString();
	if (!error.isEmpty())
		qWarning("%s", qUtf8Printable(error));

	qInfo("Finished match");
	connect(m_tournament->gameManager(), SIGNAL(finished()),
		this, SIGNAL(finished()));
	m_tournament->gameManager()->finish();
}

void EngineMatch::print(const QString& msg)
{
	qInfo("%lld %s", m_startTime.elapsed(), qUtf8Printable(msg));
}

void EngineMatch::printRanking()
{
	qInfo("%s", qUtf8Printable(m_tournament->results()));
}
