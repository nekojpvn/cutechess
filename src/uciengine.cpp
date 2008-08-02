/*
    This file is part of SloppyGUI.

    SloppyGUI is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    SloppyGUI is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with SloppyGUI.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <QString>
#include <QStringList>
#include <QDebug>

#include "uciengine.h"
#include "chessboard/chessmove.h"

UciEngine::UciEngine(QIODevice* ioDevice, Chessboard* chessboard, QObject* parent)
	: ChessEngine(ioDevice, chessboard, parent)
{
	setName("UciEngine");
	// Tell the engine to turn on Uci mode
	write("uci");
	write("isready");
}

UciEngine::~UciEngine()
{
	//write("quit");
}

void UciEngine::newGame(Chessboard::ChessSide side)
{
	setSide(side);
	m_moves.clear();
	write("ucinewgame");
	write(QString("position ") + m_chessboard->fenString());
}

void UciEngine::sendOpponentsMove(const ChessMove& move)
{
	QString moveString;
	if (m_notation == LongNotation)
		moveString = m_chessboard->coordMoveString(move);
	else if (m_notation == StandardNotation)
		moveString = m_chessboard->sanMoveString(move);
	
	m_moves.append(moveString);
	write(QString("position ") + m_chessboard->fenString() + QString(" moves ") + m_moves.join(" "));
}

void UciEngine::go()
{
	write("go movetime 2000");
}

void UciEngine::setTimeControl(TimeControl timeControl)
{

}

void UciEngine::setTimeLeft(int timeLeft)
{

}

ChessEngine::ChessProtocol UciEngine::protocol() const
{
	return ChessEngine::Uci;
}

void UciEngine::parseLine(const QString& line)
{
	QString command = line.section(' ', 0, 0);
	QString args = line.right(line.length() - command.length() - 1);

	if (command == "bestmove")
	{
		QString moveString = args.section(' ', 0, 0);
		if (moveString.isEmpty())
			moveString = args;

		m_moves.append(moveString);
		ChessMove move = m_chessboard->chessMoveFromString(moveString);
		emit moveMade(move);
	}
	else if (command == "readyok")
	{
		m_isReady = true;
	}
	else if (command == "id")
	{
		QString tag = args.section(' ', 0, 0);
		QString tagVal = args.section(' ', 1);
		
		if (tag == "name")
			m_name = tagVal;
	}
}

