/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file transactionTests.cpp
 * @author Dmitrii Khokhlov <winsvega@mail.ru>
 * @date 2015
 * Transaction test functions.
 */

#include <libethcore/SealEngine.h>
#include <libethashseal/GenesisInfo.h>
#include <libethereum/ChainParams.h>
#include <test/tools/libtestutils/Common.h>
#include <test/tools/libtesteth/TestHelper.h>
#include <test/tools/fuzzTesting/fuzzHelper.h>

using namespace std;
using namespace json_spirit;
using namespace dev;
using namespace dev::eth;

namespace dev {  namespace test {

json_spirit::mValue doTransactionTests(json_spirit::mValue const& _input, bool _fillin)
{
	json_spirit::mValue v = _input; // TODO: avoid copying and only add valid fields into the new object.
	unique_ptr<SealEngineFace> se(ChainParams(genesisInfo(eth::Network::MainNetworkTest)).createSealEngine());
	for (auto& i: v.get_obj())
	{
		string testname = i.first;
		json_spirit::mObject& o = i.second.get_obj();

		if (!TestOutputHelper::passTest(testname))
		{
			o.clear(); //don't add irrelevant tests to the final file when filling
			continue;
		}

		BOOST_REQUIRE(o.count("blocknumber") > 0);
		u256 transactionBlock = toInt(o["blocknumber"].get_str());
		BlockHeader bh;
		bh.setNumber(transactionBlock);
		bh.setGasLimit(u256("0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));
		bool onMetropolis = (transactionBlock >= se->chainParams().u256Param("metropolisForkBlock"));

		if (_fillin)
		{
			BOOST_REQUIRE(o.count("transaction") > 0);
			mObject tObj = o["transaction"].get_obj();

			//Construct Rlp of the given transaction
			RLPStream rlpStream = createRLPStreamFromTransactionFields(tObj);
			o["rlp"] = toHexPrefixed(rlpStream.out());

			try
			{
				Transaction txFromFields(rlpStream.out(), CheckTransaction::Everything);
				bool onMetropolisAndZeroSig = onMetropolis && txFromFields.hasZeroSignature();

				if (!txFromFields.signature().isValid())
				if (!onMetropolisAndZeroSig)
					BOOST_THROW_EXCEPTION(Exception() << errinfo_comment(testname + "transaction from RLP signature is invalid") );
				se->verifyTransaction(ImportRequirements::Everything, txFromFields, bh, 0);

				if (o.count("sender") > 0)
				{
					string expectSender = toString(o["sender"].get_str());
					BOOST_CHECK_MESSAGE(toString(txFromFields.sender()) == expectSender, "Error filling transaction test " + TestOutputHelper::testName() + ": expected another sender address! (got: " + toString(txFromFields.sender()) + "), expected: (" + expectSender + ")");
				}
				o["sender"] = toString(txFromFields.sender());
				o["transaction"] = ImportTest::makeAllFieldsHex(tObj);
				o["hash"] = toString(txFromFields.sha3());
			}
			catch(Exception const& _e)
			{
				//Transaction is InValid
				cnote << "Transaction Exception: " << diagnostic_information(_e);
				o.erase(o.find("transaction"));
				if (o.count("sender") > 0)
					o.erase(o.find("sender"));
				if (o.count("expect") > 0)
				{
					bool expectInValid = (o["expect"].get_str() == "invalid");
					BOOST_CHECK_MESSAGE(expectInValid, testname + " Check state: Transaction '" << i.first << "' is expected to be valid!");
					o.erase(o.find("expect"));
				}
			}

			//Transaction is Valid
			if (o.count("expect") > 0)
			{
				bool expectValid = (o["expect"].get_str() == "valid");
				BOOST_CHECK_MESSAGE(expectValid, testname + " Check state: Transaction '" << i.first << "' is expected to be invalid!");
				o.erase(o.find("expect"));
			}
		}
		else
		{
			BOOST_REQUIRE(o.count("rlp") > 0);
			Transaction txFromRlp;
			try
			{
				bytes stream = importByteArray(o["rlp"].get_str());
				RLP rlp(stream);
				txFromRlp = Transaction(rlp.data(), CheckTransaction::Everything);
				bool onMetropolisAndZeroSig = onMetropolis && txFromRlp.hasZeroSignature();
				se->verifyTransaction(ImportRequirements::Everything, txFromRlp, bh, 0);
				if (!txFromRlp.signature().isValid())
				if (!onMetropolisAndZeroSig)
					BOOST_THROW_EXCEPTION(Exception() << errinfo_comment(testname + "transaction from RLP signature is invalid") );
			}
			catch(Exception const& _e)
			{
				cnote << i.first;
				cnote << "Transaction Exception: " << diagnostic_information(_e);
				BOOST_CHECK_MESSAGE(o.count("transaction") == 0, testname + "A transaction object should not be defined because the RLP is invalid!");
				continue;
			}
			catch(...)
			{
				BOOST_CHECK_MESSAGE(o.count("transaction") == 0, testname + "A transaction object should not be defined because the RLP is invalid!");
				continue;
			}

			BOOST_REQUIRE_MESSAGE(o.count("transaction") > 0, testname + "Expected a valid transaction!");

			mObject tObj = o["transaction"].get_obj();
			h256 txSha3Expected = h256(o["hash"].get_str());
			Transaction txFromFields(createRLPStreamFromTransactionFields(tObj).out(), CheckTransaction::Everything);

			//Check the fields restored from RLP to original fields
			BOOST_CHECK_MESSAGE(txFromFields.data() == txFromRlp.data(), testname + "Data in given RLP not matching the Transaction data!");
			BOOST_CHECK_MESSAGE(txFromFields.value() == txFromRlp.value(), testname + "Value in given RLP not matching the Transaction value!");
			BOOST_CHECK_MESSAGE(txFromFields.gasPrice() == txFromRlp.gasPrice(), testname + "GasPrice in given RLP not matching the Transaction gasPrice!");
			BOOST_CHECK_MESSAGE(txFromFields.gas() == txFromRlp.gas(), testname + "Gas in given RLP not matching the Transaction gas!");
			BOOST_CHECK_MESSAGE(txFromFields.nonce() == txFromRlp.nonce(), testname + "Nonce in given RLP not matching the Transaction nonce!");
			BOOST_CHECK_MESSAGE(txFromFields.receiveAddress() == txFromRlp.receiveAddress(), testname + "Receive address in given RLP not matching the Transaction 'to' address!");
			BOOST_CHECK_MESSAGE(txFromFields.sender() == txFromRlp.sender(), testname + "Transaction sender address in given RLP not matching the Transaction 'vrs' signature!");
			BOOST_CHECK_MESSAGE(txFromFields.sha3() == txFromRlp.sha3(), testname + "Transaction sha3 hash in given RLP not matching the Transaction 'vrs' signature!");
			BOOST_CHECK_MESSAGE(txFromFields.sha3() == txSha3Expected, testname + "Expected different transaction hash!");
			BOOST_CHECK_MESSAGE(txFromFields == txFromRlp, testname + "However, txFromFields != txFromRlp!");
			BOOST_REQUIRE (o.count("sender") > 0);

			Address addressReaded = Address(o["sender"].get_str());
			BOOST_CHECK_MESSAGE(txFromFields.sender() == addressReaded || txFromRlp.sender() == addressReaded, testname + "Signature address of sender does not match given sender address!");
		}
	}//for
	return v;
}//doTransactionTests

} }// Namespace Close

class transactiontestfixture
{
public:
	transactiontestfixture()
	{
		string casename = boost::unit_test::framework::current_test_case().p_name;

		if ((casename == "ttWrongRLPFrontier" || casename == "ttWrongRLPHomestead") && test::Options::get().filltests)
			copyAllFilesFromFolder(casename);
		else
			fillAllFilesInFolder(casename);
	}

	void fillAllFilesInFolder(string const& _folder)
	{
		std::string fillersPath = test::getTestPath() + "/src/TransactionTestsFiller/" + _folder;

		string filter = test::Options::get().singleTestName.empty() ? string() : test::Options::get().singleTestName + "Filler";
		std::vector<boost::filesystem::path> files = test::getJsonFiles(fillersPath, filter);
		int fileCount = files.size();

		if (test::Options::get().filltests)
			fileCount *= 2; //tests are checked when filled and after they been filled
		test::TestOutputHelper::initTest(fileCount);

		for (auto const& file: files)
		{
			test::TestOutputHelper::setCurrentTestFileName(file.filename().string());
			test::executeTests(file.filename().string(), "/TransactionTests/"+_folder, "/TransactionTestsFiller/"+_folder, dev::test::doTransactionTests);
		}

		test::TestOutputHelper::finishTest();
	}

	void copyAllFilesFromFolder(string const& _folder)
	{
		std::string fillersPath =  dev::test::getTestPath() + "/src/TransactionTestsFiller/" + _folder;
		std::vector<boost::filesystem::path> files = test::getJsonFiles(fillersPath);

		for (auto const& file : files)
		{
			dev::test::TestOutputHelper::initTest();
			string copyto = dev::test::getTestPath() + "/TransactionTests/" + _folder + "/" + file.filename().string();
			clog << "Copying " + fillersPath + "/" + file.filename().string() + "\n";
			clog << " TO " << copyto + "\n";
			dev::test::copyFile(fillersPath + "/" + file.filename().string(), dev::test::getTestPath() + "/TransactionTests/" + _folder + "/" + file.filename().string());
			BOOST_REQUIRE_MESSAGE(boost::filesystem::exists(copyto), "Error when copying the test file!");
			dev::test::TestOutputHelper::finishTest();
		}
		return;
	}
};

BOOST_FIXTURE_TEST_SUITE(TransactionTests, transactiontestfixture)

BOOST_AUTO_TEST_CASE(ttConstantinople){}
BOOST_AUTO_TEST_CASE(ttEip155VitaliksEip158){}
BOOST_AUTO_TEST_CASE(ttEip155VitaliksHomesead){}
BOOST_AUTO_TEST_CASE(ttEip158){}
BOOST_AUTO_TEST_CASE(ttFrontier){}
BOOST_AUTO_TEST_CASE(ttHomestead){}
BOOST_AUTO_TEST_CASE(ttSpecConstantinople){}
BOOST_AUTO_TEST_CASE(ttVRuleEip158){}
BOOST_AUTO_TEST_CASE(ttWrongRLPFrontier){}
BOOST_AUTO_TEST_CASE(ttWrongRLPHomestead){}
BOOST_AUTO_TEST_CASE(ttZeroSigConstantinople){}

BOOST_AUTO_TEST_SUITE_END()
