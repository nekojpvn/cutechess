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

#include "westernboard.h"
#include <QStringList>
#include "westernzobrist.h"


namespace Chess {

WesternBoard::WesternBoard(WesternZobrist* zobrist,
			   QObject* parent)
	: Board(zobrist, parent),
	  m_arwidth(0),
	  m_sign(1),
	  m_enpassantSquare(0),
	  m_reversibleMoveCount(0),
	  m_kingCanCapture(true),
	  m_zobrist(zobrist)
{
	setPieceType(Pawn, tr("pawn"), "P");
	setPieceType(Knight, tr("knight"), "N", KnightMovement);
	setPieceType(Bishop, tr("bishop"), "B", BishopMovement);
	setPieceType(Rook, tr("rook"), "R", RookMovement);
	setPieceType(Queen, tr("queen"), "Q", BishopMovement | RookMovement);
	setPieceType(King, tr("king"), "K");
}

int WesternBoard::width() const
{
	return 8;
}

int WesternBoard::height() const
{
	return 8;
}

bool WesternBoard::kingCanCapture() const
{
	return true;
}

void WesternBoard::vInitialize()
{
	m_kingCanCapture = kingCanCapture();
	m_arwidth = width() + 2;

	m_castlingRights.rookSquare[White][QueenSide] = 0;
	m_castlingRights.rookSquare[White][KingSide] = 0;
	m_castlingRights.rookSquare[Black][QueenSide] = 0;
	m_castlingRights.rookSquare[Black][KingSide] = 0;

	m_kingSquare[White] = 0;
	m_kingSquare[Black] = 0;

	m_castleTarget[White][QueenSide] = (height() + 1) * m_arwidth + 3;
	m_castleTarget[White][KingSide] = (height() + 1) * m_arwidth + width() - 1;
	m_castleTarget[Black][QueenSide] = 2 * m_arwidth + 3;
	m_castleTarget[Black][KingSide] = 2 * m_arwidth + width() - 1;

	m_knightOffsets.resize(8);
	m_knightOffsets[0] = -2 * m_arwidth - 1;
	m_knightOffsets[1] = -2 * m_arwidth + 1;
	m_knightOffsets[2] = -m_arwidth - 2;
	m_knightOffsets[3] = -m_arwidth + 2;
	m_knightOffsets[4] = m_arwidth - 2;
	m_knightOffsets[5] = m_arwidth + 2;
	m_knightOffsets[6] = 2 * m_arwidth - 1;
	m_knightOffsets[7] = 2 * m_arwidth + 1;

	m_bishopOffsets.resize(4);
	m_bishopOffsets[0] = -m_arwidth - 1;
	m_bishopOffsets[1] = -m_arwidth + 1;
	m_bishopOffsets[2] = m_arwidth - 1;
	m_bishopOffsets[3] = m_arwidth + 1;

	m_rookOffsets.resize(4);
	m_rookOffsets[0] = -m_arwidth;
	m_rookOffsets[1] = -1;
	m_rookOffsets[2] = 1;
	m_rookOffsets[3] = m_arwidth;
}

int WesternBoard::captureType(const Move& move) const
{
	if (pieceAt(move.sourceSquare()).type() == Pawn
	&&  move.targetSquare() == m_enpassantSquare)
		return Pawn;

	return Board::captureType(move);
}

WesternBoard::CastlingSide WesternBoard::castlingSide(const Move& move) const
{
	int target = move.targetSquare();
	const int* rookSq = m_castlingRights.rookSquare[sideToMove()];

	if (target == rookSq[QueenSide])
		return QueenSide;
	if (target == rookSq[KingSide])
		return KingSide;
	return NoCastlingSide;
}

QString WesternBoard::lanMoveString(const Move& move)
{
	CastlingSide cside = castlingSide(move);
	if (cside != NoCastlingSide && !isRandomVariant())
	{
		Move tmp(move.sourceSquare(),
			 m_castleTarget[sideToMove()][cside]);
		return Board::lanMoveString(tmp);
	}

	return Board::lanMoveString(move);
}

QString WesternBoard::sanMoveString(const Move& move)
{
	QString str;
	int source = move.sourceSquare();
	int target = move.targetSquare();
	Piece piece = pieceAt(source);
	Piece capture = pieceAt(target);
	Square square = chessSquare(source);

	char checkOrMate = 0;
	makeMove(move);
	if (inCheck(sideToMove()))
	{
		if (canMove())
			checkOrMate = '+';
		else
			checkOrMate = '#';
	}
	undoMove();

	// drop move
	if (source == 0 && move.promotion() != Piece::NoPiece)
	{
		str = lanMoveString(move);
		if (checkOrMate != 0)
			str += checkOrMate;
		return str;
	}

	bool needRank = false;
	bool needFile = false;
	Side side = sideToMove();

	if (piece.type() == Pawn)
	{
		if (target == m_enpassantSquare)
			capture = Piece(otherSide(side), Pawn);
		if (capture.isValid())
			needFile = true;
	}
	else if (piece.type() == King)
	{
		CastlingSide cside = castlingSide(move);
		if (cside != NoCastlingSide)
		{
			if (cside == QueenSide)
				str = "O-O-O";
			else
				str = "O-O";
			if (checkOrMate != 0)
				str += checkOrMate;
			return str;
		}
		else
			str += pieceSymbol(piece).toUpper();
	}
	else	// not king or pawn
	{
		str += pieceSymbol(piece).toUpper();
		QVarLengthArray<Move> moves;
		generateMoves(moves, piece.type());

		for (int i = 0; i < moves.size(); i++)
		{
			const Move& move2 = moves[i];
			if (move2.sourceSquare() == source
			||  move2.targetSquare() != target)
				continue;

			if (!vIsLegalMove(move2))
				continue;

			Square square2(chessSquare(move2.sourceSquare()));
			if (square2.file() != square.file())
				needFile = true;
			else if (square2.rank() != square.rank())
				needRank = true;
		}
	}
	if (needFile)
		str += 'a' + square.file();
	if (needRank)
		str += '1' + square.rank();

	if (capture.isValid())
		str += 'x';

	str += squareString(target);

	if (move.promotion() != Piece::NoPiece)
		str += "=" + pieceSymbol(move.promotion()).toUpper();

	if (checkOrMate != 0)
		str += checkOrMate;

	return str;
}

Move WesternBoard::moveFromLanString(const QString& str)
{
	Move move(Board::moveFromLanString(str));

	Side side = sideToMove();
	int source = move.sourceSquare();
	int target = move.targetSquare();

	if (source == m_kingSquare[side]
	&&  qAbs(source - target) != 1)
	{
		const int* rookSq = m_castlingRights.rookSquare[side];
		if (target == m_castleTarget[side][QueenSide])
			target = rookSq[QueenSide];
		else if (target == m_castleTarget[side][KingSide])
			target = rookSq[KingSide];

		if (target != 0)
			return Move(source, target);
	}

	return move;
}

Move WesternBoard::moveFromSanString(const QString& str)
{
	if (str.length() < 2)
		return Move();

	QString mstr = str;
	Side side = sideToMove();

	// Ignore check/mate/strong move/blunder notation
	while (mstr.endsWith('+') || mstr.endsWith('#')
	||     mstr.endsWith('!') || mstr.endsWith('?'))
	{
		mstr.chop(1);
	}

	if (mstr.length() < 2)
		return Move();

	// Castling
	if (mstr.startsWith("O-O"))
	{
		CastlingSide cside;
		if (mstr == "O-O")
			cside = KingSide;
		else if (mstr == "O-O-O")
			cside = QueenSide;
		else
			return Move();

		int source = m_kingSquare[side];
		int target = m_castlingRights.rookSquare[side][cside];

		Move move(source, target);
		if (isLegalMove(move))
			return move;
		else
			return Move();
	}

	Square sourceSq;
	Square targetSq;
	QString::const_iterator it = mstr.begin();

	// A SAN move can't start with the capture mark, and
	// a pawn move must not specify the piece type
	if (*it == 'x' || pieceFromSymbol(*it) == Pawn)
		return Move();

	// Piece type
	Piece piece = pieceFromSymbol(*it);
	if (piece.side() != White)
		piece = Piece::NoPiece;
	else
		piece.setSide(side);
	if (piece.isEmpty())
	{
		piece = Piece(side, Pawn);
		targetSq = chessSquare(mstr.mid(0, 2));
		if (isValidSquare(targetSq))
			it += 2;
	}
	else
		++it;

	bool stringIsCapture = false;

	if (!isValidSquare(targetSq))
	{
		// Source square's file
		sourceSq.setFile(it->toAscii() - 'a');
		if (sourceSq.file() < 0 || sourceSq.file() >= width())
			sourceSq.setFile(-1);
		else if (++it == mstr.end())
			return Move();

		// Source square's rank
		if (it->isDigit())
		{
			sourceSq.setRank(it->toAscii() - '1');
			if (sourceSq.rank() < 0 || sourceSq.rank() >= height())
				return Move();
			++it;
		}
		if (it == mstr.end())
		{
			// What we thought was the source square, was
			// actually the target square.
			if (isValidSquare(sourceSq))
			{
				targetSq = sourceSq;
				sourceSq.setRank(-1);
				sourceSq.setFile(-1);
			}
			else
				return Move();
		}
		// Capture
		else if (*it == 'x')
		{
			if(++it == mstr.end())
				return Move();
			stringIsCapture = true;
		}

		// Target square
		if (!isValidSquare(targetSq))
		{
			if (it + 1 == mstr.end())
				return Move();
			targetSq = chessSquare(mstr.mid(it - mstr.begin(), 2));
			it += 2;
		}
	}
	if (!isValidSquare(targetSq))
		return Move();
	int target = squareIndex(targetSq);

	// Make sure that the move string is right about whether
	// or not the move is a capture.
	bool isCapture = false;
	if (pieceAt(target).side() == otherSide(side)
	||  (target == m_enpassantSquare && piece.type() == Pawn))
		isCapture = true;
	if (isCapture != stringIsCapture)
		return Move();

	// Promotion
	int promotion = Piece::NoPiece;
	if (it != mstr.end())
	{
		if ((*it == '=' || *it == '(') && ++it == mstr.end())
			return Move();

		promotion = pieceFromSymbol(*it).type();
		if (promotion == Piece::NoPiece)
			return Move();
	}

	QVarLengthArray<Move> moves;
	generateMoves(moves, piece.type());
	const Move* match = 0;

	// Loop through all legal moves to find a move that matches
	// the data we got from the move string.
	for (int i = 0; i < moves.size(); i++)
	{
		const Move& move = moves[i];
		if (move.targetSquare() != target)
			continue;
		Square sourceSq2 = chessSquare(move.sourceSquare());
		if (sourceSq.rank() != -1 && sourceSq2.rank() != sourceSq.rank())
			continue;
		if (sourceSq.file() != -1 && sourceSq2.file() != sourceSq.file())
			continue;
		// Castling moves were handled earlier
		if (pieceAt(target) == Piece(side, Rook))
			continue;
		if (move.promotion() != promotion)
			continue;

		if (!vIsLegalMove(move))
			continue;

		// Return an empty move if there are multiple moves that
		// match the move string.
		if (match != 0)
			return Move();
		match = &move;
	}

	if (match != 0)
		return *match;

	return Move();
}

QString WesternBoard::castlingRightsString(FenNotation notation) const
{
	QString str;

	for (int side = White; side <= Black; side++)
	{
		for (int cside = KingSide; cside >= QueenSide; cside--)
		{
			int rs = m_castlingRights.rookSquare[side][cside];
			if (rs == 0)
				continue;

			int offset = (cside == QueenSide) ? -1: 1;
			Piece piece;
			int i = rs + offset;
			bool ambiguous = false;

			// If the castling rook is not the outernmost rook,
			// the castling square is ambiguous
			while (!(piece = pieceAt(i)).isWall())
			{
				if (piece == Piece(Side(side), Rook))
				{
					ambiguous = true;
					break;
				}
				i += offset;
			}

			QChar c;
			// If the castling square is ambiguous, then we can't
			// use 'K' or 'Q'. Instead we'll use the square's file.
			if (ambiguous || notation == ShredderFen)
				c = QChar('a' + chessSquare(rs).file());
			else
			{
				if (cside == 0)
					c = 'q';
				else
					c = 'k';
			}
			if (side == upperCaseSide())
				c = c.toUpper();
			str += c;
		}
	}

	if (str.length() == 0)
		str = "-";
	return str;
}

QString WesternBoard::vFenString(FenNotation notation) const
{
	// Castling rights
	QString fen = castlingRightsString(notation) + ' ';

	// En-passant square
	if (m_enpassantSquare != 0)
		fen += squareString(m_enpassantSquare);
	else
		fen += '-';

	// Reversible halfmove count
	fen += ' ';
	fen += QString::number(m_reversibleMoveCount);

	// Full move number
	fen += ' ';
	fen += QString::number(m_history.size() / 2 + 1);

	return fen;
}

bool WesternBoard::parseCastlingRights(QChar c)
{
	int offset = 0;
	CastlingSide cside = NoCastlingSide;
	Side side = (c.isUpper()) ? upperCaseSide() : otherSide(upperCaseSide());
	c = c.toLower();

	if (c == 'q')
	{
		cside = QueenSide;
		offset = -1;
	}
	else if (c == 'k')
	{
		cside = KingSide;
		offset = 1;
	}

	int kingSq = m_kingSquare[side];

	if (offset != 0)
	{
		Piece piece;
		int i = kingSq + offset;
		int rookSq = 0;

		// Locate the outernmost rook on the castling side
		while (!(piece = pieceAt(i)).isWall())
		{
			if (piece == Piece(side, Rook))
				rookSq = i;
			i += offset;
		}
		if (rookSq != 0)
		{
			setCastlingSquare(side, cside, rookSq);
			return true;
		}
	}
	else	// Shredder FEN or X-FEN
	{
		int file = c.toAscii() - 'a';
		if (file < 0 || file >= width())
			return false;

		// Get the rook's source square
		int rookSq;
		if (side == White)
			rookSq = (height() + 1) * m_arwidth + 1 + file;
		else
			rookSq = 2 * m_arwidth + 1 + file;

		// Make sure the king and the rook are on the same rank
		if (abs(kingSq - rookSq) >= width())
			return false;

		// Update castling rights in the FenData object
		if (pieceAt(rookSq) == Piece(side, Rook))
		{
			if (rookSq > kingSq)
				cside = KingSide;
			else
				cside = QueenSide;
			setCastlingSquare(side, cside, rookSq);
			return true;
		}
	}

	return false;
}

bool WesternBoard::vSetFenString(const QStringList& fen)
{
	if (fen.size() < 2)
		return false;
	QStringList::const_iterator token = fen.begin();

	// Find the king squares
	int kingCount[2] = {0, 0};
	for (int sq = 0; sq < arraySize(); sq++)
	{
		Piece tmp = pieceAt(sq);
		if (tmp.type() == King)
		{
			m_kingSquare[tmp.side()] = sq;
			kingCount[tmp.side()]++;
		}
	}
	if (kingCount[White] != 1 || kingCount[Black] != 1)
		return false;

	// Castling rights
	m_castlingRights.rookSquare[White][QueenSide] = 0;
	m_castlingRights.rookSquare[White][KingSide] = 0;
	m_castlingRights.rookSquare[Black][QueenSide] = 0;
	m_castlingRights.rookSquare[Black][KingSide] = 0;
	if (*token != "-")
	{
		QString::const_iterator c;
		for (c = token->begin(); c != token->end(); ++c)
		{
			if (!parseCastlingRights(*c))
				return false;
		}
	}

	// En-passant square
	++token;
	m_enpassantSquare = 0;
	Side side(sideToMove());
	m_sign = (side == White) ? 1 : -1;
	if (*token != "-")
	{
		setEnpassantSquare(squareIndex(*token));
		if (m_enpassantSquare == 0)
			return false;

		// Ignore the en-passant square if an en-passant
		// capture isn't possible.
		int pawnSq = m_enpassantSquare + m_arwidth * m_sign;
		Piece ownPawn(side, Pawn);
		if (pieceAt(pawnSq - 1) != ownPawn
		&&  pieceAt(pawnSq + 1) != ownPawn)
			setEnpassantSquare(0);
	}

	// Reversible halfmove count
	++token;
	if (token != fen.end())
	{
		bool ok;
		int tmp = token->toInt(&ok);
		if (!ok || tmp < 0)
			return false;
		m_reversibleMoveCount = tmp;
	}
	else
		m_reversibleMoveCount = 0;

	// The full move number is ignored. It's rarely useful

	m_history.clear();
	return true;
}

void WesternBoard::setEnpassantSquare(int square)
{
	if (square == m_enpassantSquare)
		return;

	if (m_enpassantSquare != 0)
		xorKey(m_zobrist->enpassant(m_enpassantSquare));
	if (square != 0)
		xorKey(m_zobrist->enpassant(square));

	m_enpassantSquare = square;
}

void WesternBoard::setCastlingSquare(Side side,
				     CastlingSide cside,
				     int square)
{
	int& rs = m_castlingRights.rookSquare[side][cside];
	if (rs == square)
		return;

	if (rs != 0)
		xorKey(m_zobrist->castling(side, rs));
	if (square != 0)
		xorKey(m_zobrist->castling(side, square));
	rs = square;
}

void WesternBoard::removeCastlingRights(int square)
{
	Piece piece = pieceAt(square);
	if (piece.type() != Rook)
		return;

	Side side(piece.side());
	const int* cr = m_castlingRights.rookSquare[side];

	if (square == cr[QueenSide])
		setCastlingSquare(side, QueenSide, 0);
	else if (square == cr[KingSide])
		setCastlingSquare(side, KingSide, 0);
}

void WesternBoard::vMakeMove(const Move& move, QVarLengthArray<int>& changedSquares)
{
	Side side = sideToMove();
	int source = move.sourceSquare();
	int target = move.targetSquare();
	Piece capture = pieceAt(target);
	int promotionType = move.promotion();
	int pieceType = pieceAt(source).type();
	int epSq = m_enpassantSquare;
	int* rookSq = m_castlingRights.rookSquare[side];
	bool clearSource = true;
	bool isReversible = true;

	Q_ASSERT(target != 0);

	MoveData md = { capture, epSq, m_castlingRights,
			NoCastlingSide, m_reversibleMoveCount };

	if (source == 0)
	{
		Q_ASSERT(promotionType != Piece::NoPiece);

		pieceType = promotionType;
		promotionType = Piece::NoPiece;
		clearSource = false;
		isReversible = false;
		epSq = 0;
	}

	setEnpassantSquare(0);

	if (pieceType == King)
	{
		// In case of a castling move, make the rook's move
		CastlingSide cside = castlingSide(move);
		if (cside != NoCastlingSide)
		{
			md.castlingSide = cside;
			int rookSource = target;
			target = m_castleTarget[side][cside];
			int rookTarget = (cside == QueenSide) ? target + 1 : target -1;
			if (rookTarget == source || target == source)
				clearSource = false;

			Piece rook = Piece(side, Rook);
			setSquare(rookSource, Piece::NoPiece);
			setSquare(rookTarget, rook);
			changedSquares.append(target);
			changedSquares.append(rookTarget);
			isReversible = false;
		}
		m_kingSquare[side] = target;
		// Any king move removes all castling rights
		setCastlingSquare(side, QueenSide, 0);
		setCastlingSquare(side, KingSide, 0);
	}
	else if (pieceType == Pawn)
	{
		isReversible = false;

		// Make an en-passant capture
		if (target == epSq)
		{
			int epTarget = target + m_arwidth * m_sign;
			setSquare(epTarget, Piece::NoPiece);
			changedSquares.append(epTarget);
		}
		// Push a pawn two squares ahead, creating an en-passant
		// opportunity for the opponent.
		else if ((source - target) * m_sign == m_arwidth * 2)
		{
			Piece opPawn(otherSide(side), Pawn);
			if (pieceAt(target - 1) == opPawn
			||  pieceAt(target + 1) == opPawn)
				setEnpassantSquare(source - m_arwidth * m_sign);
		}
		else if (promotionType != Piece::NoPiece)
			pieceType = promotionType;
	}
	else if (pieceType == Rook)
	{
		// Remove castling rights from the rook's square
		for (int i = QueenSide; i <= KingSide; i++)
		{
			if (source == rookSq[i])
			{
				setCastlingSquare(side, CastlingSide(i), 0);
				isReversible = false;
				break;
			}
		}
	}

	if (captureType(move) != Piece::NoPiece)
	{
		removeCastlingRights(target);
		isReversible = false;
	}

	setSquare(target, Piece(side, pieceType));
	if (clearSource)
		setSquare(source, Piece::NoPiece);

	if (isReversible)
		m_reversibleMoveCount++;
	else
		m_reversibleMoveCount = 0;

	m_history.append(md);
	m_sign *= -1;
}

void WesternBoard::vUndoMove(const Move& move)
{
	const MoveData& md = m_history.last();
	int source = move.sourceSquare();
	int target = move.targetSquare();

	m_sign *= -1;
	Side side = sideToMove();

	setEnpassantSquare(md.enpassantSquare);
	m_reversibleMoveCount = md.reversibleMoveCount;
	m_castlingRights = md.castlingRights;

	CastlingSide cside = md.castlingSide;
	if (cside != NoCastlingSide)
	{
		m_kingSquare[side] = source;
		// Move the rook back after castling
		int tmp = m_castleTarget[side][cside];
		setSquare(tmp, Piece::NoPiece);
		tmp = (cside == QueenSide) ? tmp + 1 : tmp - 1;
		setSquare(tmp, Piece::NoPiece);

		setSquare(target, Piece(side, Rook));
		setSquare(source, Piece(side, King));
		m_history.pop_back();
		return;
	}
	else if (target == m_kingSquare[side])
	{
		m_kingSquare[side] = source;
	}
	else if (target == m_enpassantSquare)
	{
		// Restore the pawn captured by the en-passant move
		int epTarget = target + m_arwidth * m_sign;
		setSquare(epTarget, Piece(otherSide(side), Pawn));
	}

	if (move.promotion() != Piece::NoPiece)
	{
		if (source != 0)
			setSquare(source, Piece(side, Pawn));
	}
	else
		setSquare(source, pieceAt(target));

	setSquare(target, md.capture);
	m_history.pop_back();
}

void WesternBoard::generateMovesForPiece(QVarLengthArray<Move>& moves,
					 int pieceType,
					 int square) const
{
	if (pieceType == Pawn)
		return generatePawnMoves(square, moves);
	if (pieceType == King)
	{
		generateHoppingMoves(square, m_bishopOffsets, moves);
		generateHoppingMoves(square, m_rookOffsets, moves);
		generateCastlingMoves(moves);
		return;
	}

	if (pieceHasMovement(pieceType, KnightMovement))
		generateHoppingMoves(square, m_knightOffsets, moves);
	if (pieceHasMovement(pieceType, BishopMovement))
		generateSlidingMoves(square, m_bishopOffsets, moves);
	if (pieceHasMovement(pieceType, RookMovement))
		generateSlidingMoves(square, m_rookOffsets, moves);
}

bool WesternBoard::inCheck(Side side, int square) const
{
	Side opSide = otherSide(side);
	if (square == 0)
		square = m_kingSquare[side];
	
	// Pawn attacks
	int step = (side == White) ? -m_arwidth : m_arwidth;
	// Left side
	if (pieceAt(square + step - 1) == Piece(opSide, Pawn))
		return true;
	// Right side
	if (pieceAt(square + step + 1) == Piece(opSide, Pawn))
		return true;

	Piece piece;
	
	// Knight, archbishop, chancellor attacks
	for (int i = 0; i < m_knightOffsets.size(); i++)
	{
		piece = pieceAt(square + m_knightOffsets[i]);
		if (piece.side() == opSide && pieceHasMovement(piece.type(), KnightMovement))
			return true;
	}
	
	// Bishop, queen, archbishop, king attacks
	for (int i = 0; i < m_bishopOffsets.size(); i++)
	{
		int offset = m_bishopOffsets[i];
		int targetSquare = square + offset;
		if (m_kingCanCapture && targetSquare == m_kingSquare[opSide])
			return true;
		while ((piece = pieceAt(targetSquare)).isEmpty()
		||     piece.side() == opSide)
		{
			if (!piece.isEmpty())
			{
				if (pieceHasMovement(piece.type(), BishopMovement))
					return true;
				break;
			}
			targetSquare += offset;
		}
	}
	
	// Rook, queen, chancellor, king attacks
	for (int i = 0; i < m_rookOffsets.size(); i++)
	{
		int offset = m_rookOffsets[i];
		int targetSquare = square + offset;
		if (m_kingCanCapture && targetSquare == m_kingSquare[opSide])
			return true;
		while ((piece = pieceAt(targetSquare)).isEmpty()
		||     piece.side() == opSide)
		{
			if (!piece.isEmpty())
			{
				if (pieceHasMovement(piece.type(), RookMovement))
					return true;
				break;
			}
			targetSquare += offset;
		}
	}
	
	return false;
}

bool WesternBoard::isLegalPosition()
{
	Side side = otherSide(sideToMove());
	if (inCheck(side))
		return false;

	if (m_history.isEmpty())
		return true;

	const Move& move = lastMove();

	// Make sure that no square between the king's initial and final
	// squares (including the initial and final squares) are under
	// attack (in check) by the opponent.
	CastlingSide cside = m_history.last().castlingSide;
	if (cside != NoCastlingSide)
	{
		int source = move.sourceSquare();
		int target = m_castleTarget[side][cside];
		int offset = (source <= target) ? 1 : -1;
		
		if (source == target)
		{
			int i = target - offset;
			forever
			{
				i -= offset;
				Piece piece(pieceAt(i));

				if (piece.isWall())
					return true;
				if (piece.side() == sideToMove()
				&&  pieceHasMovement(piece.type(), RookMovement))
					return false;
			}
		}
		
		for (int i = source; i != target; i += offset)
		{
			if (inCheck(side, i))
				return false;
		}
	}

	return true;
}

bool WesternBoard::vIsLegalMove(const Move& move)
{
	Q_ASSERT(!move.isNull());

	if (!m_kingCanCapture
	&&  move.sourceSquare() == m_kingSquare[sideToMove()]
	&&  captureType(move) != Piece::NoPiece)
		return false;

	return Board::vIsLegalMove(move);
}

void WesternBoard::addPromotions(int sourceSquare,
				 int targetSquare,
				 QVarLengthArray<Move>& moves) const
{
	moves.append(Move(sourceSquare, targetSquare, Knight));
	moves.append(Move(sourceSquare, targetSquare, Bishop));
	moves.append(Move(sourceSquare, targetSquare, Rook));
	moves.append(Move(sourceSquare, targetSquare, Queen));
}

void WesternBoard::generatePawnMoves(int sourceSquare,
				     QVarLengthArray<Move>& moves) const
{
	int targetSquare;
	Piece capture;
	int step = m_sign * m_arwidth;
	bool isPromotion = pieceAt(sourceSquare - step * 2).isWall();

	// One square ahead
	targetSquare = sourceSquare - step;
	capture = pieceAt(targetSquare);
	if (capture.isEmpty())
	{
		if (isPromotion)
			addPromotions(sourceSquare, targetSquare, moves);
		else
		{
			moves.append(Move(sourceSquare, targetSquare));

			// Two squares ahead
			if (pieceAt(sourceSquare + step * 2).isWall())
			{
				targetSquare -= step;
				capture = pieceAt(targetSquare);
				if (capture.isEmpty())
					moves.append(Move(sourceSquare, targetSquare));
			}
		}
	}

	// Captures, including en-passant moves
	Side opSide(otherSide(sideToMove()));
	for (int i = -1; i <= 1; i += 2)
	{
		targetSquare = sourceSquare - step + i;
		capture = pieceAt(targetSquare);
		if (capture.side() == opSide
		||  targetSquare == m_enpassantSquare)
		{
			if (isPromotion)
				addPromotions(sourceSquare, targetSquare, moves);
			else
				moves.append(Move(sourceSquare, targetSquare));
		}
	}
}

bool WesternBoard::canCastle(CastlingSide castlingSide) const
{
	Side side = sideToMove();
	int rookSq = m_castlingRights.rookSquare[side][castlingSide];
	if (rookSq == 0)
		return false;

	int kingSq = m_kingSquare[side];
	int target = m_castleTarget[side][castlingSide];
	int left;
	int right;
	int rtarget;

	// Find all the squares involved in the castling
	if (castlingSide == QueenSide)
	{
		rtarget = target + 1;

		if (target < rookSq)
			left = target;
		else
			left = rookSq;

		if (rtarget > kingSq)
			right = rtarget;
		else
			right = kingSq;
	}
	else	// Kingside
	{
		rtarget = target - 1;

		if (rtarget < kingSq)
			left = rtarget;
		else
			left = kingSq;

		if (target > rookSq)
			right = target;
		else
			right = rookSq;
	}

	// Make sure that the smallest back rank interval containing the king,
	// the castling rook, and their destination squares contains no pieces
	// other than the king and the castling rook.
	for (int i = left; i <= right; i++)
	{
		if (i != kingSq && i != rookSq && !pieceAt(i).isEmpty())
			return false;
	}

	return true;
}

void WesternBoard::generateCastlingMoves(QVarLengthArray<Move>& moves) const
{
	Side side = sideToMove();
	int source = m_kingSquare[side];
	for (int i = QueenSide; i <= KingSide; i++)
	{
		if (canCastle(CastlingSide(i)))
		{
			int target = m_castlingRights.rookSquare[side][i];
			moves.append(Move(source, target));
		}
	}
}

int WesternBoard::kingSquare(Side side) const
{
	Q_ASSERT(side != NoSide);
	return m_kingSquare[side];
}

int WesternBoard::reversibleMoveCount() const
{
	return m_reversibleMoveCount;
}

Result WesternBoard::result()
{
	QString str;

	// Checkmate/Stalemate
	if (!canMove())
	{
		if (inCheck(sideToMove()))
		{
			Side winner = otherSide(sideToMove());
			str = tr("%1 mates").arg(sideString(winner));

			return Result(Result::Win, winner, str);
		}
		else
		{
			str = tr("Draw by stalemate");
			return Result(Result::Draw, NoSide, str);
		}
	}

	// Insufficient mating material
	int material[2] = { 0, 0 };
	for (int i = 0; i < arraySize(); i++)
	{
		const Piece& piece = pieceAt(i);
		if (!piece.isValid())
			continue;

		if (piece.type() == Knight || piece.type() == Bishop)
			material[piece.side()] += 1;
		else
			material[piece.side()] += 2;
	}
	if (material[White] <= 3 && material[Black] <= 3)
	{
		str = tr("Draw by insufficient mating material");
		return Result(Result::Draw, NoSide, str);
	}

	// 50 move rule
	if (m_reversibleMoveCount >= 100)
	{
		str = tr("Draw by fifty moves rule");
		return Result(Result::Draw, NoSide, str);
	}

	// 3-fold repetition
	if (repeatCount() >= 2)
	{
		str = tr("Draw by 3-fold repetition");
		return Result(Result::Draw, NoSide, str);
	}

	return Result();
}

} // namespace Chess